#include "IoSide.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>

#include <hdr/hdr_histogram.h>
#include <httplib.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include "Affinity.hpp"
#include "LineSimulator.hpp"
#include "SchematicLayout.hpp"
#include "contracts.hpp"
#include "sunverify/procedure/Describe.hpp"
#include "sunverify/procedure/XlsxReporter.hpp"

namespace sunverify::app {
namespace c = sunverify::contracts;
using nlohmann::json;

namespace {

std::string iso_now() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
  return buf;
}

std::int64_t epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string uuid4() {
  static thread_local std::mt19937_64 rng{std::random_device{}() ^ static_cast<std::uint64_t>(now_ns())};
  const std::uint64_t a = rng(), b = rng();
  char buf[37];
  std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                static_cast<unsigned>((a >> 16) & 0xffff), static_cast<unsigned>(a & 0xfff),
                static_cast<unsigned>(0x8000 | ((b >> 48) & 0x3fff)), static_cast<unsigned long long>(b & 0xffffffffffffULL));
  return buf;
}

bool is_local(const std::string& addr) { return addr == "127.0.0.1" || addr == "::1" || addr == "localhost"; }

void cors(const httplib::Request& req, httplib::Response& res) {
  const auto origin = req.get_header_value("Origin");
  if (origin.rfind("http://127.0.0.1", 0) == 0 || origin.rfind("http://localhost", 0) == 0)
    res.set_header("Access-Control-Allow-Origin", origin);
  res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

struct Histogram {
  hdr_histogram* h = nullptr;
  Histogram() { hdr_init(1, 60'000'000, 3, &h); }   // 1 us .. 60 s, 3 significant figures
  ~Histogram() { hdr_close(h); }
  void record_ms(double ms) { hdr_record_value(h, static_cast<std::int64_t>(ms * 1000.0)); }
  double p(double pct) const { return h->total_count ? static_cast<double>(hdr_value_at_percentile(h, pct)) / 1000.0 : 0.0; }
  std::int64_t count() const { return h->total_count; }
  void reset() { hdr_reset(h); }
};

}  // namespace

struct IoSide::Impl {
  Pipeline& p;
  const StationConfig& cfg;
  const procedure::Profile& prof;
  std::atomic<bool> running{false};
  std::atomic<bool> shutdown{false};
  std::thread io_thread, uplink_thread, reasoning_thread, control_thread, mjpeg_thread;

  // per-IP projection for the part map and crops
  std::vector<double> u_px, v_px;
  int frame_w = 1920;

  // latest picture (io thread writes, servers read)
  mutable std::mutex state_m;
  std::string state_json = "{}";
  std::int64_t last_frame_ns = 0;
  json verdict = nullptr;
  bool cycle_violated = false;
  std::string cycle_id;
  std::uint64_t cycle_no = 0;
  std::int16_t scenario = -1;
  std::int64_t cycle_t0_ns = 0;

  // latency
  Histogram engine_h, evidence_h, bucket_evidence_h;
  double last_engine_ms = 0, last_evidence_ms = 0;
  std::map<std::uint32_t, json> waiting;   // violations waiting for the PLC's acknowledgement

  // records for the shift report
  std::mutex rec_m;
  procedure::ShiftReport report;

  // uplink: everything recorded goes through a spool file, oldest first (as the fleet agent does)
  std::mutex up_m;
  std::condition_variable up_cv;
  std::deque<std::pair<std::string, std::string>> up_q;
  std::atomic<std::uint64_t> uplinked{0}, spooled{0};

  // reasoning: bounded drop-oldest queue, circuit breaker
  std::mutex rs_m;
  std::condition_variable rs_cv;
  std::deque<json> rs_q;
  std::atomic<int> failures{0};
  std::atomic<std::int64_t> open_until_ns{0};
  std::atomic<std::uint64_t> adjudications{0};

  std::unique_ptr<ix::WebSocketServer> ws;
  std::unique_ptr<httplib::Server> control, mjpeg;
  std::unique_ptr<LineSimulator> line;
  std::ofstream record;
  std::int64_t record_t0 = 0;

  // health bucket
  std::int64_t bucket_t0 = 0;
  std::uint64_t b_verified = 0, b_captured = 0, b_dropped = 0, b_obs = 0, b_amb = 0;
  double b_conf = 0, b_anchor_sum = 0;
  std::uint64_t b_anchor_n = 0, last_obs = 0, last_amb = 0, b_adj = 0;
  double last_conf = 0;

  explicit Impl(Pipeline& pp) : p(pp), cfg(pp.config()), prof(pp.profile()) {
    project_points();
    if (std::string(pp.provider()) == "schematic") {
      // the frame on screen tiles every camera (synth/schematic.py): overlay each point where it is drawn,
      // in that frame's full-resolution pixels, not in its own camera's frame
      std::ifstream in(cfg.profile_dir / "procedure.json");
      const SchematicLayout lay = schematic_layout(json::parse(in));
      const double k = 1920.0 / cfg.process_width;
      for (std::size_t i = 0; i < lay.pts.size() && i < u_px.size(); ++i) {
        u_px[i] = lay.pts[i].x * k;
        v_px[i] = lay.pts[i].y * k;
      }
      frame_w = 1920;
    }
    report.station = cfg.station_id;
    report.profile = prof.name();
    report.profile_version = prof.version();
  }

  void project_points() {
    // the CAD position through the camera's pose: on-screen positions match the part (spec Section 21)
    std::ifstream in(cfg.profile_dir / "procedure.json");
    const json j = json::parse(in);
    const auto& cams = j["cameras"];
    for (const auto& ip : prof.ips()) {
      double u = 0, v = 0;
      for (const auto& cam : cams) {
        if (cam["id"] != ip.camera) continue;
        const double W = cam["width_px"].get<double>(), H = W / cam["aspect"].get<double>(), f = cam["f_px"].get<double>();
        frame_w = static_cast<int>(W);
        const auto P = cam["position_mm"], L = cam["look_at_mm"];
        double fw[3] = {L[0].get<double>() - P[0].get<double>(), L[1].get<double>() - P[1].get<double>(), L[2].get<double>() - P[2].get<double>()};
        const double n = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
        for (double& x : fw) x /= n;
        const double up[3] = {0, std::abs(fw[2]) < 0.99 ? 0.0 : 1.0, std::abs(fw[2]) < 0.99 ? 1.0 : 0.0};
        double r[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
        const double rn = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        for (double& x : r) x /= rn;
        const double uu[3] = {r[1] * fw[2] - r[2] * fw[1], r[2] * fw[0] - r[0] * fw[2], r[0] * fw[1] - r[1] * fw[0]};
        const double d[3] = {ip.position_mm[0] - P[0].get<double>(), ip.position_mm[1] - P[1].get<double>(), ip.position_mm[2] - P[2].get<double>()};
        const double z = d[0] * fw[0] + d[1] * fw[1] + d[2] * fw[2];
        if (z <= 0) break;
        u = W / 2 + f * (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / z;
        v = H / 2 - f * (d[0] * uu[0] + d[1] * uu[1] + d[2] * uu[2]) / z;
        break;
      }
      u_px.push_back(u);
      v_px.push_back(v);
    }
  }

  // ---- uplink -----------------------------------------------------------------------------------
  void enqueue(const std::string& path, const json& body) {
    if (!cfg.uplink_enabled) return;               // a measurement run is not plant history
    const json line = {{"path", path}, {"body", body}};
    {
      std::lock_guard lk(up_m);
      std::filesystem::create_directories(cfg.event_spool.parent_path());
      std::ofstream(cfg.event_spool, std::ios::app | std::ios::binary) << line.dump() << '\n';
      ++spooled;
    }
    up_cv.notify_one();
  }

  void uplink_loop() {
    name_current_thread("uplink");
    while (running) {
      {
        std::unique_lock lk(up_m);
        up_cv.wait_for(lk, std::chrono::seconds(2));
      }
      std::vector<std::string> lines;
      {
        std::lock_guard lk(up_m);
        std::ifstream in(cfg.event_spool, std::ios::binary);
        for (std::string l; std::getline(in, l);)
          if (!l.empty()) lines.push_back(l);
      }
      if (lines.empty()) continue;
      httplib::Client cli(cfg.api_url);
      cli.set_connection_timeout(1, 0);
      cli.set_read_timeout(3, 0);
      std::size_t sent = 0;
      for (const auto& l : lines) {
        json rec;
        try {
          rec = json::parse(l);
        } catch (...) {
          ++sent;                                       // a torn line from a crash: drop it, keep order
          continue;
        }
        const auto res = cli.Post(rec["path"].get<std::string>(), {{"X-API-Key", cfg.api_key}}, rec["body"].dump(),
                                  "application/json");
        if (!res || res->status >= 500) break;          // the API is away: stop, keep order, retry later
        ++sent;                                         // 2xx, or a 4xx that retrying cannot fix
        ++uplinked;
      }
      if (sent == 0) continue;
      std::lock_guard lk(up_m);
      // records appended while we were sending stay at the end of the file
      std::vector<std::string> now;
      {
        std::ifstream in(cfg.event_spool, std::ios::binary);
        for (std::string l; std::getline(in, l);)
          if (!l.empty()) now.push_back(l);
      }
      std::ofstream out(cfg.event_spool, std::ios::trunc | std::ios::binary);
      for (std::size_t i = sent; i < now.size(); ++i) out << now[i] << '\n';
    }
  }

  // ---- reasoning: Florence-2, never on the alert path (I3, I11) ----------------------------------
  void ask(const IoMsg& m) {
    if (!cfg.sidecar_enabled) return;
    const auto& ip = prof.ips()[m.ask_ip];
    const double scale = static_cast<double>(cfg.process_width) / frame_w;
    const int cx = static_cast<int>(u_px[m.ask_ip] * scale), cy = static_cast<int>(v_px[m.ask_ip] * scale);
    json req = {{"request_id", uuid4()}, {"station_id", cfg.station_id}, {"ip", ip.id}, {"family", ip.family},
                {"candidate_state", ip.states[static_cast<std::size_t>(m.ask_obs.rank)]}, {"candidate_rank", m.ask_obs.rank},
                {"ip_index", m.ask_ip}, {"conf", m.ask_obs.conf}, {"ring_seq", m.ring_seq},
                {"crop", {{"x", cx - 48}, {"y", cy - 48}, {"w", 96}, {"h", 96}}}};
    std::lock_guard lk(rs_m);
    if (rs_q.size() >= static_cast<std::size_t>(cfg.sidecar_queue)) rs_q.pop_front();   // drop the oldest
    rs_q.push_back(std::move(req));
    rs_cv.notify_one();
  }

  void reasoning_loop() {
    name_current_thread("reasoning");
    httplib::Client cli(cfg.sidecar_url);
    cli.set_connection_timeout(0, cfg.sidecar_timeout_ms * 1000);
    cli.set_read_timeout(0, cfg.sidecar_timeout_ms * 1000);
    while (running) {
      json req;
      {
        std::unique_lock lk(rs_m);
        rs_cv.wait_for(lk, std::chrono::milliseconds(200), [&] { return !rs_q.empty() || !running; });
        if (rs_q.empty()) continue;
        req = std::move(rs_q.front());
        rs_q.pop_front();
      }
      if (now_ns() < open_until_ns) continue;           // circuit open: the point simply stays held
      json body = req;
      body.erase("candidate_rank");
      body.erase("ip_index");
      const auto res = cli.Post("/v1/adjudicate", body.dump(), "application/json");
      if (!res || res->status != 200) {
        if (++failures >= cfg.sidecar_failures) {
          open_until_ns = now_ns() + static_cast<std::int64_t>(cfg.sidecar_open_s) * 1'000'000'000;
          failures = 0;
          std::fprintf(stderr, "[engine] sidecar: circuit open for %d s\n", cfg.sidecar_open_s);
        }
        continue;
      }
      failures = 0;
      ++adjudications;
      try {
        const json r = json::parse(res->body);
        AdjResult a;
        a.ip = req["ip_index"].get<std::uint16_t>();
        a.candidate = static_cast<std::int8_t>(req["candidate_rank"].get<int>());
        a.confirmed = r.value("confirmed", false);
        a.t_ns = now_ns();
        p.adj_ring().try_push(a);                       // this thread is the ring's only producer
      } catch (...) {
      }
    }
  }

  // ---- the io loop ------------------------------------------------------------------------------
  json violation_record(const IoMsg& m) {
    const json d = procedure::describe(prof, m.violation);
    json detail = d;
    for (const char* k : {"kind", "step", "t_ms"}) detail.erase(k);
    std::string observed, expected;
    if (d.contains("observed")) observed = d["observed"];
    else if (d.contains("ip")) observed = d["ip"];
    if (d.contains("expected")) expected = d["expected"];
    json a = {{"event_id", uuid4()},
              {"ts", iso_now()},
              {"station_id", cfg.station_id},
              {"cycle_id", cycle_id},
              {"profile", prof.name()},
              {"profile_version", prof.version()},
              {"step_id", d["step"]},
              {"violation_type", d["kind"]},
              {"observed_ip", observed.empty() ? json(nullptr) : json(observed)},
              {"expected_ip", expected.empty() ? json(nullptr) : json(expected)},
              {"detail", detail},
              {"engine_ms", 0.0},
              {"evidence_ms", 0.0},
              {"ring_end_seq", p.clip_ring() ? p.clip_ring()->write_seq() : 0},
              {"narrative", procedure::narrate(prof, m.violation)},
              {"adjudicated", false},
              {"cad_revision", prof.cad_revision()},
              {"model_version", cfg.model_version}};
    return a;
  }

  std::vector<std::string> corrective(const IoMsg& snap, int step) const {
    std::vector<std::string> out;
    const auto& st = prof.steps()[static_cast<std::size_t>(step)];
    for (std::size_t k = 0; k < st.targets.size(); ++k)
      if (snap.committed[st.targets[k]] < st.need[k]) out.push_back(prof.ips()[st.targets[k]].id);
    return out;
  }

  void publish_state(const IoMsg& s) {
    c::StationState st;
    st.station_id = cfg.station_id;
    st.frame_seq = static_cast<std::int64_t>(s.frame_seq);
    st.ts_ms = epoch_ms();
    st.cycle_id = cycle_id;
    st.cycle_elapsed_ms = s.cycle_ms;
    st.takt_s = cfg.takt_s;
    st.profile = prof.name();
    st.profile_version = prof.version();
    st.cad_revision = prof.cad_revision();
    st.model_version = cfg.model_version;
    st.provider = p.provider();
    st.degraded = std::nullopt;
    st.advisory_only = cfg.advisory_only;
    std::vector<c::StepState> steps;
    st.step = "";
    for (std::size_t i = 0; i < prof.steps().size(); ++i) {
      const auto& ps = prof.steps()[i];
      c::StepState ss;
      ss.id = ps.id;
      ss.kind = ps.kind == procedure::StepKind::kSequence ? c::StepKind::SEQUENCE : c::StepKind::COMPLETE;
      ss.state = ps.state;
      std::vector<std::string> targets;
      for (const auto t : ps.targets) targets.push_back(prof.ips()[t].id);
      ss.targets = targets;
      ss.done = s.step_done[i];
      ss.total = static_cast<std::int64_t>(ps.targets.size());
      const auto status = static_cast<procedure::StepStatus>(s.step_status[i]);
      ss.status = status == procedure::StepStatus::kComplete ? c::StepStatus::COMPLETE
                  : status == procedure::StepStatus::kActive ? c::StepStatus::ACTIVE
                                                              : c::StepStatus::WAITING;
      if (cycle_violated && verdict.is_object() && verdict["step"] == ps.id) ss.status = c::StepStatus::VIOLATED;
      ss.allotted_ms = ps.max_duration_ms;
      if (st.step.empty() && status != procedure::StepStatus::kComplete) st.step = ps.id;
      steps.push_back(std::move(ss));
    }
    st.steps = steps;
    for (std::uint16_t i = 0; i < s.n_ips; ++i) {
      const auto& ip = prof.ips()[i];
      c::InspectionPointState x;
      x.id = ip.id;
      x.family = ip.family;
      x.state = ip.states[static_cast<std::size_t>(s.committed[i] < 0 ? 0 : s.committed[i])];
      x.rank = s.committed[i] < 0 ? 0 : s.committed[i];
      x.n_states = static_cast<std::int64_t>(ip.states.size());
      x.conf = s.conf[i];
      x.ambiguous = s.ambiguous[i] != 0;
      x.u = u_px[i];
      x.v = v_px[i];
      x.x_mm = ip.position_mm[0];
      x.y_mm = ip.position_mm[1];
      st.ips.push_back(std::move(x));
    }
    for (const auto n : s.next)
      if (n >= 0) st.next.push_back(prof.ips()[static_cast<std::size_t>(n)].id);
    st.latency.engine_ms_p50 = engine_h.p(50);
    st.latency.engine_ms_p99 = engine_h.p(99);
    st.latency.evidence_ms_p99 = evidence_h.p(99);
    st.latency.last_engine_ms = last_engine_ms;
    st.latency.last_evidence_ms = last_evidence_ms;
    st.andon = !p.counters().plc_connected ? c::Andon::AMBER : cycle_violated ? c::Andon::RED : c::Andon::GREEN;
    json j = st;
    j["verdict"] = verdict;                             // null until the cycle has one
    j["scenario"] = scenario >= 0 ? json(p.detector().scenarios()[static_cast<std::size_t>(scenario)].name) : json(nullptr);
    j["hands"] = s.hands;
    j["plc_connected"] = p.counters().plc_connected.load();
    j["frozen"] = p.frozen();
    const std::string text = j.dump();
    {
      std::lock_guard lk(state_m);
      state_json = text;
      last_frame_ns = now_ns();
    }
    if (ws)
      for (auto&& client : ws->getClients()) client->send(text);
    if (record.is_open()) {
      if (!record_t0) record_t0 = epoch_ms();
      record << "{\"t\":" << (epoch_ms() - record_t0) << ",\"state\":" << text << "}\n";
    }
  }

  void health_bucket(bool force) {
    const std::int64_t now = now_ns();
    if (!force && now - bucket_t0 < static_cast<std::int64_t>(cfg.bucket_s) * 1'000'000'000) return;
    auto& k = p.counters();
    const std::uint64_t verified = k.frames_verified, captured = k.frames_captured, dropped = k.frames_dropped;
    const double secs = static_cast<double>(now - bucket_t0) / 1e9;
    const std::uint64_t obs = last_obs - b_obs, amb = last_amb - b_amb;
    const double conf = last_conf - b_conf;
    const std::uint64_t cap = (captured - b_captured) + (dropped - b_dropped);
    json h = {{"station_id", cfg.station_id},
              {"bucket_start", iso_now()},
              {"bucket_s", cfg.bucket_s},
              {"fps", secs > 0 ? static_cast<double>(verified - b_verified) / secs : 0.0},
              {"dropped_frame_rate", cap ? static_cast<double>(dropped - b_dropped) / static_cast<double>(cap) : 0.0},
              {"ip_conf_mean", obs ? json(conf / static_cast<double>(obs)) : json(nullptr)},
              {"ambiguous_rate", obs ? static_cast<double>(amb) / static_cast<double>(obs) : 0.0},
              {"anchor_residual_px", b_anchor_n ? json(b_anchor_sum / static_cast<double>(b_anchor_n)) : json(nullptr)},
              {"evidence_p99_ms", bucket_evidence_h.count() ? json(bucket_evidence_h.p(99)) : json(nullptr)},
              {"camera_temp_c", nullptr},
              {"packets_resent", nullptr},
              {"adjudications", adjudications - b_adj},
              {"model_version", cfg.model_version},
              {"cad_revision", prof.cad_revision()}};
    if (bucket_t0 != 0 && secs > 0.5) enqueue("/v1/health/station", h);
    bucket_t0 = now;
    b_verified = verified;
    b_captured = captured;
    b_dropped = dropped;
    b_obs = last_obs;
    b_amb = last_amb;
    b_conf = last_conf;
    b_anchor_sum = 0;
    b_anchor_n = 0;
    b_adj = adjudications;
    bucket_evidence_h.reset();
  }

  void io_loop() {
    name_current_thread("io");
    auto msg = std::make_unique<IoMsg>();
    auto last_snap = std::make_unique<IoMsg>();
    AckMsg ack;
    bucket_t0 = now_ns();
    while (running) {
      bool worked = false;
      while (p.io_ring().try_pop(*msg)) {
        worked = true;
        switch (msg->kind) {
          case IoKind::kCycleStart: {
            cycle_id = uuid4();
            cycle_no = msg->cycle_no;
            scenario = msg->scenario;
            cycle_t0_ns = msg->t_ns;
            cycle_violated = false;
            verdict = nullptr;
            if (line && scenario >= 0) {
              const auto& s = p.detector().scenarios()[static_cast<std::size_t>(scenario)];
              std::vector<LineEvent> ev;
              for (const auto& t : s.tags) {
                ev.push_back({t.t_ms, t.tag, t.value});
                if (t.value) ev.push_back({t.t_ms + 300, t.tag, false});   // a nutrunner OK is a pulse
              }
              ev.push_back({s.release_at_ms, cfg.tag_prefix + ".ReleaseRequest", true});
              line->begin_cycle(msg->t_ns, std::move(ev));
            }
            break;
          }
          case IoKind::kViolation: {
            json a = violation_record(*msg);
            cycle_violated = true;
            const auto& st = prof.steps()[static_cast<std::size_t>(msg->violation.step)];
            verdict = {{"kind", a["violation_type"]}, {"step", st.id}, {"observed_ip", a["observed_ip"]},
                       {"expected_ip", a["expected_ip"]}, {"narrative", a["narrative"]},
                       {"corrective", corrective(*last_snap, msg->violation.step)}};
            if (cfg.advisory_only) enqueue_anomaly(std::move(a));
            else waiting[msg->seq] = std::move(a);
            break;
          }
          case IoKind::kRelease: {
            const double dur = static_cast<double>(msg->t_ns - cycle_t0_ns) / 1e9;
            json cyc = {{"cycle_id", cycle_id}, {"ts", iso_now()}, {"station_id", cfg.station_id},
                        {"profile", prof.name()}, {"passed", msg->release_ok && !cycle_violated}, {"duration_s", dur}};
            enqueue("/v1/events/cycle", cyc);
            if (!cycle_violated && msg->release_ok) verdict = {{"kind", "RELEASED"}, {"step", ""}, {"narrative", "Released: every step complete, in order."}};
            std::lock_guard lk(rec_m);
            report.cycles.push_back({cycle_id, iso_now(),
                                     scenario >= 0 ? p.detector().scenarios()[static_cast<std::size_t>(scenario)].name : "",
                                     msg->release_ok && !cycle_violated, dur});
            break;
          }
          case IoKind::kAsk:
            ask(*msg);
            break;
          case IoKind::kFrame:
            *last_snap = *msg;
            last_obs = msg->observations;
            last_amb = msg->ambiguous_obs;
            last_conf = msg->conf_sum;
            b_anchor_sum += msg->anchor_residual_px;
            ++b_anchor_n;
            publish_state(*msg);
            break;
        }
      }
      while (p.ack_ring().try_pop(ack)) {
        worked = true;
        if (ack.kind != AlertKind::kViolation) continue;
        if (ack.ok) {
          engine_h.record_ms(ack.engine_ms);
          evidence_h.record_ms(ack.evidence_ms);
          bucket_evidence_h.record_ms(ack.evidence_ms);
          last_engine_ms = ack.engine_ms;
          last_evidence_ms = ack.evidence_ms;
        }
        auto it = waiting.find(ack.seq);
        if (it == waiting.end()) continue;
        json a = std::move(it->second);
        waiting.erase(it);
        a["engine_ms"] = ack.engine_ms;
        a["evidence_ms"] = ack.evidence_ms;
        enqueue_anomaly(std::move(a));
      }
      health_bucket(false);
      if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  void enqueue_anomaly(json a) {
    {
      std::lock_guard lk(rec_m);
      report.anomalies.push_back({a["ts"], a["cycle_id"], a["step_id"], a["violation_type"],
                                  a["observed_ip"].is_null() ? "" : a["observed_ip"].get<std::string>(),
                                  a["expected_ip"].is_null() ? "" : a["expected_ip"].get<std::string>(), a["narrative"],
                                  a["engine_ms"].get<double>(), a["evidence_ms"].get<double>()});
    }
    enqueue("/v1/events/anomaly", a);
  }

  std::string health() const {
    auto& k = p.counters();
    const double up_s = static_cast<double>(now_ns() - p.started_ns()) / 1e9;
    std::int64_t last;
    {
      std::lock_guard lk(state_m);
      last = last_frame_ns;
    }
    const bool verifying = last && now_ns() - last < 2'000'000'000;
    json h = {{"status", verifying ? "up" : "stalled"},
              {"station_id", cfg.station_id},
              {"profile", prof.name()},
              {"profile_version", prof.version()},
              {"model_version", cfg.model_version},
              {"cad_revision", prof.cad_revision()},
              {"provider", p.provider()},
              {"degraded", p.gpu_detector() && p.gpu_detector()->degraded() ? json("tensorrt -> cuda") : json(nullptr)},
              {"detector_ms", p.gpu_detector() ? json(p.gpu_detector()->last_detector_ms()) : json(nullptr)},
              {"classifier_ms", p.gpu_detector() ? json(p.gpu_detector()->last_classifier_ms()) : json(nullptr)},
              {"fps", up_s > 0 ? static_cast<double>(k.frames_verified) / up_s : 0.0},
              {"evidence_p99_ms", evidence_h.p(99)},
              {"engine_p50_ms", engine_h.p(50)},
              {"engine_p99_ms", engine_h.p(99)},
              {"alerts", evidence_h.count()},
              {"uptime_s", up_s},
              {"frames_verified", k.frames_verified.load()},
              {"frames_dropped", k.frames_dropped.load()},
              {"heartbeats", k.beats.load()},
              {"plc_connected", k.plc_connected.load()},
              {"cycles", k.cycles.load()},
              {"spool_written", spooled.load()},
              {"uplinked", uplinked.load()},
              {"adjudications", adjudications.load()},
              {"sidecar_circuit_open", now_ns() < open_until_ns},
              {"source", p.source_description()},
              {"frozen", p.frozen()}};
    return h.dump();
  }

  // ---- servers ----------------------------------------------------------------------------------
  void start_servers() {
    ix::initNetSystem();
    ws = std::make_unique<ix::WebSocketServer>(cfg.ws_port, "127.0.0.1");
    ws->setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState>, ix::WebSocket&, const ix::WebSocketMessagePtr&) {});
    const auto ok = ws->listen();
    if (ok.first) ws->start();
    else std::fprintf(stderr, "[engine] WebSocket :%d: %s\n", cfg.ws_port, ok.second.c_str());

    control = std::make_unique<httplib::Server>();
    auto& s = *control;
    s.Options(".*", [](const httplib::Request& req, httplib::Response& res) { cors(req, res); });
    s.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      res.set_content(health(), "application/json");
    });
    s.Get("/state", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      std::lock_guard lk(state_m);
      res.set_content(state_json, "application/json");
    });
    s.Get("/scenarios", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      json a = json::array();
      for (const auto& sc : p.detector().scenarios())
        a.push_back({{"name", sc.name}, {"title", sc.title}, {"must_alert", sc.must_alert}, {"seconds", sc.n_frames / sc.fps}});
      res.set_content(json{{"profile", prof.name()}, {"scenarios", a}}.dump(), "application/json");
    });
    // the harness: changes the input at the next cycle boundary, never the logic; localhost only
    s.Post(R"(/scenario/([A-Za-z0-9_\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      if (!is_local(req.remote_addr)) return void(res.status = 403);
      const bool ok = p.request_scenario(req.matches[1]);
      res.status = ok ? 200 : 404;
      res.set_content(json{{"queued", ok}, {"scenario", req.matches[1].str()}}.dump(), "application/json");
    });
    s.Post("/freeze", [this](const httplib::Request& req, httplib::Response& res) {   // a frozen camera, on demand
      cors(req, res);
      if (!is_local(req.remote_addr)) return void(res.status = 403);
      p.freeze(req.get_param_value("on") != "0");
      res.set_content(json{{"frozen", p.frozen()}}.dump(), "application/json");
    });
    s.Post("/shutdown", [this](const httplib::Request& req, httplib::Response& res) {
      if (!is_local(req.remote_addr)) return void(res.status = 403);
      shutdown = true;
      res.set_content("{\"stopping\":true}", "application/json");
    });
    s.Get("/latency.hgrm", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      res.set_content(histogram_text(), "text/plain");
    });
    s.Get("/report.xlsx", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      if (!procedure::xlsx_supported()) return void(res.status = 501);
      procedure::ShiftReport r;
      {
        std::lock_guard lk(rec_m);
        r = report;
      }
      r.generated = iso_now();
      const auto path = std::filesystem::temp_directory_path() / ("torquesense_shift_" + cfg.clip_ring_name + ".xlsx");
      procedure::write_shift_report(r, path);
      std::ifstream in(path, std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      res.set_header("Content-Disposition", "attachment; filename=\"shift_" + cfg.clip_ring_name + ".xlsx\"");
      res.set_content(ss.str(), "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    });
    control_thread = std::thread([this] {
      name_current_thread("control");
      if (!control->listen("127.0.0.1", cfg.control_port))
        std::fprintf(stderr, "[engine] control port %d unavailable\n", cfg.control_port);
    });

    mjpeg = std::make_unique<httplib::Server>();
    mjpeg->Get("/stream", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      res.set_header("Cache-Control", "no-store");      // never cached (spec Section 34)
      res.set_chunked_content_provider("multipart/x-mixed-replace; boundary=frame",
                                       [this, last = std::uint64_t{0}, buf = std::vector<std::byte>(512 * 1024)](
                                           std::size_t, httplib::DataSink& sink) mutable {
        auto* ring = p.clip_ring();
        if (!ring || !running) return false;
        for (int i = 0; i < 50; ++i) {
          const std::uint64_t n = ring->write_seq();
          if (n > 0 && n - 1 != last) {
            const std::size_t len = ring->read(n - 1, buf);
            if (len) {
              last = n - 1;
              const std::string head = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + std::to_string(len) + "\r\n\r\n";
              if (!sink.write(head.data(), head.size()) || !sink.write(reinterpret_cast<const char*>(buf.data()), len) ||
                  !sink.write("\r\n", 2))
                return false;
              std::this_thread::sleep_for(std::chrono::milliseconds(60));
              return true;
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return running.load();
      });
    });
    mjpeg->Get("/frame.jpg", [this](const httplib::Request& req, httplib::Response& res) {
      cors(req, res);
      auto* ring = p.clip_ring();
      std::vector<std::byte> buf(512 * 1024);
      const std::uint64_t n = ring ? ring->write_seq() : 0;
      const std::size_t len = n ? ring->read(n - 1, buf) : 0;
      if (!len) return void(res.status = 404);
      res.set_header("Cache-Control", "no-store");
      res.set_content(std::string(reinterpret_cast<const char*>(buf.data()), len), "image/jpeg");
    });
    mjpeg_thread = std::thread([this] {
      name_current_thread("mjpeg");
      if (!mjpeg->listen("127.0.0.1", cfg.mjpeg_port)) std::fprintf(stderr, "[engine] MJPEG port %d unavailable\n", cfg.mjpeg_port);
    });
  }

  std::string histogram_text() const {
    std::string out;
    for (const auto* h : {&engine_h, &evidence_h}) {
      char line[160];
      std::snprintf(line, sizeof line, "%s latency (ms): n=%lld p50=%.2f p99=%.2f p99.9=%.2f max=%.2f\n",
                    h == &engine_h ? "engine" : "evidence", static_cast<long long>(h->count()), h->p(50), h->p(99), h->p(99.9),
                    h->p(100));
      out += line;
    }
    return out;
  }
};

IoSide::IoSide(Pipeline& p) : impl_(std::make_unique<Impl>(p)) {}
IoSide::~IoSide() { stop(); }

void IoSide::start() {
  Impl& m = *impl_;
  m.running = true;
  if (m.cfg.line_simulator && m.cfg.alert_backend != "none") {
    m.line = std::make_unique<LineSimulator>(m.cfg.opcua_endpoint, m.cfg.tag_prefix + ".ReleaseRequest");
    m.line->start();
  }
  m.start_servers();
  m.io_thread = std::thread([&m] { m.io_loop(); });
  m.uplink_thread = std::thread([&m] { m.uplink_loop(); });
  m.reasoning_thread = std::thread([&m] { m.reasoning_loop(); });
}

void IoSide::stop() {
  Impl& m = *impl_;
  if (!m.running.exchange(false)) return;
  m.up_cv.notify_all();
  m.rs_cv.notify_all();
  if (m.control) m.control->stop();
  if (m.mjpeg) m.mjpeg->stop();
  if (m.ws) m.ws->stop();
  for (auto* t : {&m.io_thread, &m.uplink_thread, &m.reasoning_thread, &m.control_thread, &m.mjpeg_thread})
    if (t->joinable()) t->join();
  if (m.line) m.line->stop();
}

bool IoSide::shutdown_requested() const noexcept { return impl_->shutdown; }
std::string IoSide::latency_summary() const {
  const auto& m = *impl_;
  auto block = [](const Histogram& h) {
    return json{{"n", h.count()}, {"p50_ms", h.p(50)}, {"p99_ms", h.p(99)}, {"p999_ms", h.p(99.9)}, {"max_ms", h.p(100)}};
  };
  return json{{"engine", block(m.engine_h)}, {"evidence", block(m.evidence_h)}}.dump();
}

bool IoSide::write_histogram(const std::filesystem::path& out) const {
  std::FILE* f = std::fopen(out.string().c_str(), "w");
  if (!f) return false;
  std::fprintf(f, "# evidence latency, milliseconds — first frame showing the transition -> OPC-UA write acknowledged\n");
  hdr_percentiles_print(impl_->evidence_h.h, f, 5, 1000.0, CLASSIC);   // value scale 1000: printed in ms
  std::fprintf(f, "\n# engine latency — commit frame -> OPC-UA write acknowledged\n");
  hdr_percentiles_print(impl_->engine_h.h, f, 5, 1000.0, CLASSIC);
  std::fclose(f);
  return true;
}

void IoSide::record_to(const std::filesystem::path& p) { impl_->record.open(p, std::ios::binary | std::ios::trunc); }
std::string IoSide::health_json() const { return impl_->health(); }

}  // namespace sunverify::app
