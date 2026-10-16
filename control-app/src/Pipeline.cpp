#include "Pipeline.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "Affinity.hpp"
#include "Conformance.hpp"
#include "SchematicLayout.hpp"
#include "sources/FrameSource.hpp"
#include "sunverify/procedure/Heartbeat.hpp"
#include "sunverify/procedure/OpcUaAlerter.hpp"

namespace sunverify::app {
namespace {

// backend: none — for tests that measure the engine without a PLC. Never used at a station.
class NullAlerter final : public procedure::IAlerter {
 public:
  bool connect() noexcept override { return true; }
  bool connected() const noexcept override { return true; }
  bool apply(const procedure::AndonState&) noexcept override { return true; }
  bool heartbeat(std::uint32_t) noexcept override { return true; }
  bool poll(std::span<procedure::PlantRead> out) noexcept override {
    for (auto& r : out) r.valid = false;
    return true;
  }
};

void pin(const char* name, int cpu, bool enabled) {
  name_current_thread(name);
  if (!enabled) return;
  std::string why;
  if (!pin_current_thread(cpu, why)) std::fprintf(stderr, "[engine] %s: could not pin to CPU %d: %s\n", name, cpu, why.c_str());
}

void copy_text(std::array<char, 160>& dst, const char* a, const char* b, const char* c) {
  std::snprintf(dst.data(), dst.size(), "%s %s %s", a, b, c);   // fixed buffer: no allocation
}

}  // namespace

Pipeline::Pipeline(StationConfig cfg) : cfg_(std::move(cfg)) {
  profile_ = procedure::Profile::load(cfg_.profile_dir / "procedure.json");
  std::vector<std::string> ids;
  std::vector<float> u, v;
  for (const auto& ip : profile_.ips()) {
    ids.push_back(ip.id);
    u.push_back(static_cast<float>(ip.position_mm[0]));
    v.push_back(static_cast<float>(ip.position_mm[1]));
  }
  detector_ = std::make_unique<perception::ReplayDetector>(ids, u, v);
  if (cfg_.provider == "schematic") {
    // Any sample folder, live: its traces are the scenarios, its media/<trace>.mp4 the frames, and the
    // states are read from the pixels (perception/src/SchematicDetector.cpp). No code per assembly.
    from_pixels_ = true;
    std::ifstream in(cfg_.profile_dir / "procedure.json");
    const SchematicLayout lay = schematic_layout(nlohmann::json::parse(in));
    schematic_.set_points(lay.pts.data(), static_cast<std::uint16_t>(lay.pts.size()), lay.radius);
    std::vector<std::filesystem::path> traces;
    for (const auto& e : std::filesystem::directory_iterator(cfg_.profile_dir / "traces"))
      if (e.path().extension() == ".yaml") traces.push_back(e.path());
    std::sort(traces.begin(), traces.end());
    for (const auto& t : traces) {
      const TraceSpec spec = load_trace(profile_, t);
      perception::ReplayScenario sc;
      sc.name = t.stem().string();
      sc.title = profile_.name() + " / " + sc.name;
      sc.fps = cfg_.fps;
      sc.release_at_ms = spec.release_at_ms;
      // as synth/sample_media.py draws it: to the release frame, then one second more
      sc.n_frames = static_cast<std::uint64_t>(std::ceil(spec.release_at_ms * cfg_.fps / 1000.0 - 1e-9)) +
                    static_cast<std::uint64_t>(cfg_.fps);
      for (const auto& g : spec.signals)
        if (g.tag >= 0) sc.tags.push_back({g.t_ms, profile_.tags()[static_cast<std::size_t>(g.tag)], g.value.number != 0});
      sc.must_alert = !spec.expect_release_ok;
      detector_->add(std::move(sc));
    }
  } else if (detector_->load_dir(cfg_.fixtures) == 0) {
    throw std::runtime_error("no replay fixtures in " + cfg_.fixtures.string() + " — run synth/scenarios.py");
  }
  for (const auto& name : cfg_.playlist)
    if (detector_->find(name) < 0) throw std::runtime_error("playlist names unknown scenario " + name);

  source_ = make_file_source(cfg_.scenarios_dir.string());
  pool_ = std::make_unique<perception::FramePool>(cfg_.process_width, cfg_.process_height, 3, 12);
  ring_ = ipc::SharedClipRing::create(cfg_.clip_ring_name, static_cast<std::uint32_t>(cfg_.clip_slots),
                                      static_cast<std::uint32_t>(cfg_.clip_slot_bytes));
  if (!ring_) std::fprintf(stderr, "[engine] clip ring %s could not be created; clips disabled\n", cfg_.clip_ring_name.c_str());

  for (const auto& t : profile_.tags()) evidence_tags_.push_back(t);
  if (cfg_.alert_backend == "none") {
    alerter_ = std::make_unique<NullAlerter>();
  } else {
    procedure::OpcUaConfig oc;
    oc.endpoint = cfg_.opcua_endpoint;
    oc.prefix = cfg_.tag_prefix;
    oc.evidence_tags = evidence_tags_;
    alerter_ = std::make_unique<procedure::OpcUaAlerter>(oc);
  }

  to_inference_ = std::make_unique<perception::SpscRingBuffer<FrameMsg, 32>>();
  to_verify_ = std::make_unique<perception::SpscRingBuffer<perception::StateVector, 32>>();
  to_alert_ = std::make_unique<perception::SpscRingBuffer<AlertCmd, 16>>();
  to_io_ = std::make_unique<perception::SpscRingBuffer<IoMsg, 64>>();
  acks_ = std::make_unique<perception::SpscRingBuffer<AckMsg, 16>>();
  plant_ = std::make_unique<perception::SpscRingBuffer<perception::PlantSignal, 64>>();
  adj_ = std::make_unique<perception::SpscRingBuffer<AdjResult, 16>>();
}

Pipeline::~Pipeline() { stop(); }

std::string Pipeline::source_description() const { return source_ ? source_->describe() : "none"; }

bool Pipeline::request_scenario(const std::string& name) {
  const int i = detector_->find(name);
  if (i < 0) return false;
  requested_ = i;
  return true;
}

void Pipeline::start() {
  prepare_process_for_realtime_work();
  started_ns_ = now_ns();
  running_ = true;
  threads_.emplace_back([this] { alert_loop(); });
  threads_.emplace_back([this] { verify_loop(); });
  threads_.emplace_back([this] { inference_loop(); });
  threads_.emplace_back([this] { capture_loop(); });
}

void Pipeline::stop() {
  if (!running_.exchange(false)) return;
  for (auto& t : threads_)
    if (t.joinable()) t.join();
  threads_.clear();
}

// ---- capture: pace, read, downscale into the pool, JPEG into the clip ring --------------------
void Pipeline::capture_loop() {
  pin("capture", cfg_.cpu_capture, cfg_.pin_threads);
  FrameTimer timer;
  const auto period = static_cast<std::int64_t>(1e9 / cfg_.fps);
  std::int64_t next = now_ns();
  std::uint64_t seq = 0, cycle = 0, frame_in_cycle = 0;
  std::size_t playlist_pos = 0;
  int scenario = -1;
  std::uint64_t n_frames = 0;
  while (running_) {
    if (scenario < 0 || frame_in_cycle >= n_frames) {           // a cycle boundary: the next part
      const int asked = requested_.exchange(-1);
      if (asked >= 0) scenario = asked;
      else if (!cfg_.playlist.empty()) scenario = detector_->find(cfg_.playlist[playlist_pos++ % cfg_.playlist.size()]);
      else scenario = 0;
      const auto& s = detector_->scenarios()[static_cast<std::size_t>(scenario)];
      source_->open(s.name);
      n_frames = s.n_frames;
      frame_in_cycle = 0;
      ++cycle;
      counters_.cycles = cycle;
    }
    timer.sleep_until_ns(next);
    next += period;
    if (frozen_) continue;                                        // a frozen camera: no frames, no beats
    const auto lease = pool_->lease();
    if (!lease) {
      counters_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
      ++frame_in_cycle;
      continue;
    }
    source_->read(pool_->data(*lease), cfg_.process_width, cfg_.process_height);
    FrameMsg m;
    m.frame_seq = seq++;
    m.t_capture_ns = now_ns();
    m.cycle_no = cycle;
    m.frame_in_cycle = frame_in_cycle;
    m.scenario = static_cast<std::int16_t>(scenario);
    m.new_cycle = frame_in_cycle == 0;
    m.pool_index = *lease;
    if (ring_) {
      const auto& jpg = source_->jpeg(cfg_.jpeg_quality);
      if (!jpg.empty()) {
        m.ring_seq = ring_->write_seq();
        ring_->publish(std::as_bytes(std::span(jpg.data(), jpg.size())), static_cast<std::uint64_t>(m.t_capture_ns), m.frame_seq);
      }
    }
    if (!to_inference_->try_push(m)) {
      pool_->release(*lease);
      counters_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    } else {
      counters_.frames_captured.fetch_add(1, std::memory_order_relaxed);
    }
    ++frame_in_cycle;
  }
}

// ---- inference: the detector fills the StateVector; the frame goes back to the pool -------------
void Pipeline::inference_loop() {
  pin("inference", cfg_.cpu_inference, cfg_.pin_threads);
  auto sv = std::make_unique<perception::StateVector>();
  FrameMsg m;
  while (running_) {
    if (!to_inference_->try_pop(m)) {
      std::this_thread::yield();
      continue;
    }
    if (m.new_cycle) detector_->select(m.scenario);
    if (from_pixels_) {
      schematic_.classify(pool_->data(m.pool_index), cfg_.process_width, cfg_.process_height, cfg_.process_width * 3, *sv);
      sv->hands = sv->tools = 0;
      sv->anchor_residual_px = 0;
    } else {
      detector_->infer(m.frame_in_cycle, *sv);
    }
    pool_->release(m.pool_index);
    sv->frame_seq = m.frame_seq;
    sv->t_capture_ns = m.t_capture_ns;
    sv->t_inferred_ns = now_ns();
    sv->ring_seq = m.ring_seq;
    sv->cycle_no = m.cycle_no;
    sv->frame_in_cycle = m.frame_in_cycle;
    sv->scenario = m.scenario;
    sv->new_cycle = m.new_cycle;
    while (running_ && !to_verify_->try_push(*sv)) std::this_thread::yield();   // the verify ring is never full in steady state
  }
}

// ---- verify: commit, interpret, decide — no I/O, no allocation, no lock ------------------------
void Pipeline::emit_violations(VerifyCore& core, std::int64_t t_ns) {
  for (std::size_t i = core.new_violations_from(); i < core.violations_total(); ++i) {
    const auto& v = core.violation(i);
    ++violation_seq_;
    if (!cfg_.advisory_only) {                                    // advisory: shown, never written to the PLC
      AlertCmd a;
      a.kind = AlertKind::kViolation;
      a.seq = violation_seq_;
      a.t_first_ns = core.violation_first_ns(i);
      a.t_commit_ns = core.violation_commit_ns(i);
      const auto& step = profile_.steps()[v.step];
      copy_text(a.text, procedure::to_string(v.kind), step.id.c_str(), v.ip >= 0 ? profile_.ips()[v.ip].id.c_str() : "");
      to_alert_->try_push(a);
    }
    IoMsg io;
    io.kind = IoKind::kViolation;
    io.seq = violation_seq_;
    io.violation = v;
    io.cycle_no = core.cycle_no();
    io.t_ns = t_ns;
    io.cycle_ms = core.cycle_ms(t_ns);
    to_io_->try_push(io);
  }
  core.mark_reported();
}

void Pipeline::verify_loop() {
  pin("verify", cfg_.cpu_verify, cfg_.pin_threads);
  VerifyCore core{profile_, {cfg_.debounce_frames, cfg_.conf_commit, cfg_.conf_floor}};
  auto sv = std::make_unique<perception::StateVector>();
  auto snap = std::make_unique<IoMsg>();
  perception::PlantSignal sig;
  AdjResult adj;
  std::uint64_t frames = 0;
  while (running_) {
    bool worked = false;
    while (plant_->try_pop(sig)) {
      worked = true;
      if (sig.tag == -2) {                                        // the release request: the last deadline
        if (sig.number == 0 || core.released()) continue;
        const bool ok = core.on_release(sig.t_ns);
        emit_violations(core, sig.t_ns);
        AlertCmd a;
        a.kind = ok ? AlertKind::kReleaseOk : AlertKind::kReleaseHeld;
        a.t_first_ns = a.t_commit_ns = sig.t_ns;
        if (!cfg_.advisory_only) to_alert_->try_push(a);
        IoMsg io;
        io.kind = IoKind::kRelease;
        io.release_ok = ok;
        io.cycle_no = core.cycle_no();
        io.t_ns = sig.t_ns;
        io.cycle_ms = core.cycle_ms(sig.t_ns);
        to_io_->try_push(io);
      } else {
        core.on_signal(sig.t_ns, sig.tag, procedure::TagValue::of(sig.number));
      }
    }
    while (adj_->try_pop(adj)) {
      worked = true;
      core.on_adjudication(adj);
      emit_violations(core, adj.t_ns);
    }
    if (to_verify_->try_pop(*sv)) {
      worked = true;
      core.on_frame(*sv);
      if (sv->new_cycle) {
        AlertCmd a;
        a.kind = AlertKind::kCycleStart;
        if (!cfg_.advisory_only) to_alert_->try_push(a);
        IoMsg io;
        io.kind = IoKind::kCycleStart;
        io.cycle_no = sv->cycle_no;
        io.scenario = sv->scenario;
        io.t_ns = sv->t_capture_ns;
        to_io_->try_push(io);
      }
      emit_violations(core, sv->t_capture_ns);
      for (const auto& ask : core.asks()) {
        IoMsg io;
        io.kind = IoKind::kAsk;
        io.ask_ip = ask.ip;
        io.ask_obs.rank = ask.candidate;
        io.ask_obs.conf = ask.conf;
        io.ask_obs.u = ask.u;
        io.ask_obs.v = ask.v;
        io.ring_seq = ask.ring_seq;
        io.frame_seq = ask.frame_seq;
        to_io_->try_push(io);
      }
      counters_.frames_verified.fetch_add(1, std::memory_order_relaxed);   // what the heartbeat watches
      if ((++frames & 1) == 0) {                                  // ~15 Hz snapshot for the dashboard and HMI
        IoMsg& s = *snap;
        s.kind = IoKind::kFrame;
        s.frame_seq = sv->frame_seq;
        s.cycle_no = core.cycle_no();
        s.t_ns = sv->t_capture_ns;
        s.cycle_ms = core.cycle_ms(sv->t_capture_ns);
        s.scenario = sv->scenario;
        s.hands = sv->hands;
        s.anchor_residual_px = sv->anchor_residual_px;
        s.release_ok = core.release_ok();
        s.n_ips = sv->n_ips;
        for (std::uint16_t i = 0; i < sv->n_ips; ++i) {
          s.committed[i] = core.committer().committed(i);
          s.conf[i] = core.committer().last_conf(i);
          s.ambiguous[i] = core.committer().ambiguous(i) ? 1 : 0;
        }
        const auto& eng = core.engine();
        int k = 0;
        s.next.fill(-1);
        for (std::size_t st = 0; st < profile_.steps().size(); ++st) {
          s.step_done[st] = static_cast<std::uint8_t>(eng.step_done_count(static_cast<int>(st)));
          s.step_status[st] = static_cast<std::uint8_t>(eng.step_status(static_cast<int>(st)));
          if (k == 0 && eng.step_status(static_cast<int>(st)) == procedure::StepStatus::kActive) {
            const int nx = eng.expected_next(static_cast<int>(st));
            if (nx >= 0) s.next[k++] = static_cast<std::int16_t>(nx);
          }
        }
        s.observations = core.committer().observations();
        s.ambiguous_obs = core.committer().ambiguous_observations();
        s.conf_sum = core.committer().conf_sum();
        to_io_->try_push(s);
      }
    }
    if (!worked) std::this_thread::yield();
  }
}

// ---- alert: the OPC-UA writes, the heartbeat, and the plant tags read back --------------------
void Pipeline::alert_loop() {
  pin("alert", cfg_.cpu_alert, cfg_.pin_threads);
  FrameTimer timer;
  procedure::HeartbeatGate gate{cfg_.heartbeat_ms};
  procedure::AndonState andon;
  std::vector<procedure::PlantRead> reads(1 + evidence_tags_.size());   // sized once, before the loop
  std::vector<double> last(reads.size(), -1.0);
  std::int64_t next_poll = 0, next_connect = 0;
  bool up = false;
  AlertCmd cmd;
  while (running_) {
    const std::int64_t now = now_ns();
    if (!up && now >= next_connect) {
      up = alerter_->connect();
      next_connect = now + 2'000'000'000;                         // reconnect every 2 s (spec Section 8)
      counters_.plc_connected = up;
      if (up) alerter_->apply(andon);
    }
    while (to_alert_->try_pop(cmd)) {
      switch (cmd.kind) {
        case AlertKind::kCycleStart:
          andon.red = false;
          andon.green = true;
          andon.hold_request = false;
          andon.cycle_state = 1;
          andon.last_violation[0] = '\0';
          break;
        case AlertKind::kViolation:
          andon.red = true;
          andon.green = false;
          andon.hold_request = true;                              // a request: the PLC owns the conveyor
          andon.cycle_state = 3;
          andon.last_violation = cmd.text;
          break;
        case AlertKind::kReleaseOk:
          andon.green = !andon.red;
          andon.hold_request = andon.red;
          andon.cycle_state = andon.red ? 3 : 2;
          break;
        case AlertKind::kReleaseHeld:
          andon.red = true;
          andon.green = false;
          andon.hold_request = true;
          andon.cycle_state = 3;
          break;
      }
      const bool ok = up && alerter_->apply(andon);
      const std::int64_t acked = now_ns();
      if (ok && cmd.kind == AlertKind::kViolation) counters_.alerts_written.fetch_add(1, std::memory_order_relaxed);
      AckMsg ack;
      ack.kind = cmd.kind;
      ack.seq = cmd.seq;
      ack.ok = ok;
      ack.engine_ms = static_cast<double>(acked - cmd.t_commit_ns) / 1e6;
      ack.evidence_ms = static_cast<double>(acked - cmd.t_first_ns) / 1e6;
      acks_->try_push(ack);
      if (!ok) up = alerter_->connected();
    }
    if (up && gate.due(now / 1'000'000, counters_.frames_verified.load(std::memory_order_relaxed))) {
      if (alerter_->heartbeat(gate.beat())) counters_.beats.fetch_add(1, std::memory_order_relaxed);
      else up = alerter_->connected();
    }
    if (up && now >= next_poll) {
      next_poll = now + 10'000'000;                               // plant tags every 10 ms
      if (alerter_->poll(reads)) {
        const std::int64_t t = now_ns();
        for (std::size_t i = 0; i < reads.size(); ++i) {
          if (!reads[i].valid || reads[i].number == last[i]) continue;
          const bool first = last[i] < 0;
          last[i] = reads[i].number;
          if (first && i == 0) continue;                          // a release request already set at start-up is stale
          perception::PlantSignal s;
          s.t_ns = t;
          s.tag = i == 0 ? -2 : static_cast<std::int32_t>(i - 1);
          s.number = reads[i].number;
          plant_->try_push(s);
        }
      } else {
        up = alerter_->connected();
      }
    }
    counters_.plc_connected = up;
    timer.sleep_until_ns(now_ns() + 1'000'000);                   // 1 ms
  }
}

}  // namespace sunverify::app
