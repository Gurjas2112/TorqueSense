#include "MediaVerify.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Conformance.hpp"
#include "SchematicLayout.hpp"
#include "VerifyCore.hpp"
#include "sunverify/cad/Compiler.hpp"
#include "sunverify/perception/SchematicDetector.hpp"
#include "sunverify/procedure/Describe.hpp"

#ifdef SUNVERIFY_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace sunverify::app {
#ifdef SUNVERIFY_WITH_OPENCV

namespace {

constexpr int kW = 960, kH = 540;

struct Loaded {
  procedure::Profile profile;
  json procedure;
  perception::SchematicDetector det;
};

Loaded load(const fs::path& sample) {
  const cad::Compiled c = cad::compile_folder(sample);
  Loaded l{procedure::Profile::from_json(c.procedure), c.procedure, {}};
  const SchematicLayout lay = schematic_layout(c.procedure);
  l.det.set_points(lay.pts.data(), static_cast<std::uint16_t>(lay.pts.size()), lay.radius);
  return l;
}

}  // namespace

namespace {

std::string state_name(const procedure::Profile& p, int ip, int rank) {
  return rank < 0 ? "?" : p.ips()[static_cast<std::size_t>(ip)].states[static_cast<std::size_t>(rank)];
}

// One video, every frame, through perception, the debounce and the interpreter.
bool verify_video(Loaded& l, const fs::path& video, const fs::path& trace, std::ofstream* csv) {
  const TraceSpec spec = load_trace(l.profile, trace);
  cv::VideoCapture cap(video.string(), cv::CAP_FFMPEG);
  if (!cap.isOpened()) {
    std::printf("  %-26s cannot open %s\n", trace.stem().string().c_str(), video.string().c_str());
    return false;
  }
  double fps = cap.get(cv::CAP_PROP_FPS);
  if (!(fps > 0)) fps = 30;
  VerifyCore core{l.profile, {}};
  auto sv = std::make_unique<perception::StateVector>();
  cv::Mat frame, small(kH, kW, CV_8UC3);
  std::size_t next_sig = 0, commits = 0, frames = 0, unread = 0;
  bool released = false, release_ok = false;
  for (std::uint64_t f = 0;; ++f) {
    if (!cap.read(frame)) break;
    ++frames;
    const auto t_ns = static_cast<std::int64_t>(static_cast<double>(f) * 1e9 / fps);
    while (next_sig < spec.signals.size() && spec.signals[next_sig].t_ms * 1'000'000 <= t_ns) {
      const auto& s = spec.signals[next_sig++];
      core.on_signal(s.t_ms * 1'000'000, s.tag, s.value);
    }
    if (!released && t_ns >= spec.release_at_ms * 1'000'000) {
      release_ok = core.on_release(spec.release_at_ms * 1'000'000);
      released = true;
    }
    const cv::Mat* img = &frame;
    if (frame.cols != kW || frame.rows != kH) {
      cv::resize(frame, small, small.size());
      img = &small;
    }
    l.det.classify(img->data, img->cols, img->rows, static_cast<int>(img->step), *sv);
    sv->frame_seq = f;
    sv->t_capture_ns = t_ns;
    sv->frame_in_cycle = f;
    sv->cycle_no = 1;
    sv->new_cycle = f == 0;
    core.on_frame(*sv);
    commits += core.commits().size();
    for (std::uint16_t i = 0; i < sv->n_ips; ++i) unread += sv->ips[i].rank < 0 ? 1 : 0;
    if (csv) {
      *csv << trace.stem().string() << ',' << f << ',' << (t_ns / 1'000'000);
      for (std::uint16_t i = 0; i < sv->n_ips; ++i) *csv << ',' << state_name(l.profile, i, sv->ips[i].rank);
      *csv << '\n';
    }
  }
  if (!released) release_ok = core.on_release(spec.release_at_ms * 1'000'000);

  json got = json::array();
  for (std::size_t i = 0; i < core.violations_total(); ++i) got.push_back(procedure::describe(l.profile, core.violation(i)));
  std::string problem;
  if (release_ok != spec.expect_release_ok) problem = std::string("release gate returned ") + (release_ok ? "true" : "false");
  else problem = expect_matches(spec.expect_violations, got);
  std::string kinds;
  for (const auto& v : got) kinds += (kinds.empty() ? "" : ",") + v["kind"].get<std::string>();
  std::printf("  %-26s %5zu frames  %3zu commits  %4zu unread  release %-5s  %-40s %s\n", trace.stem().string().c_str(),
              frames, commits, unread, release_ok ? "ok" : "held", kinds.empty() ? "no violation" : kinds.c_str(),
              problem.empty() ? "PASS" : "FAIL");
  if (!problem.empty()) std::printf("      %s\n", problem.c_str());
  return problem.empty();
}

// Every keyframe image must read exactly as frames.json says.
bool verify_keyframes(Loaded& l, const fs::path& dir, int& images) {
  std::ifstream in(dir / "frames.json");
  if (!in) return true;
  const json j = json::parse(in);
  auto sv = std::make_unique<perception::StateVector>();
  bool ok = true;
  for (const auto& row : j["frames"]) {
    const cv::Mat img = cv::imread((dir / row["file"].get<std::string>()).string(), cv::IMREAD_COLOR);
    if (img.empty()) {
      std::printf("      %s: unreadable\n", row["file"].get<std::string>().c_str());
      ok = false;
      continue;
    }
    l.det.classify(img.data, img.cols, img.rows, static_cast<int>(img.step), *sv);
    ++images;
    for (const auto& [ip, want] : row["expect"].items()) {
      const int i = l.profile.ip_index(ip);
      const std::string got = state_name(l.profile, i, sv->ips[static_cast<std::size_t>(i)].rank);
      if (got != want.get<std::string>()) {
        std::printf("      %s/%s: %s reads %s, expected %s\n", dir.filename().string().c_str(), row["file"].get<std::string>().c_str(),
                    ip.c_str(), got.c_str(), want.get<std::string>().c_str());
        ok = false;
      }
    }
  }
  return ok;
}

}  // namespace

int run_verify_media(const fs::path& sample, const std::string& which, const fs::path& frames_csv) {
  Loaded l = load(sample);
  fs::path norm = sample.lexically_normal();
  if (norm.filename().empty()) norm = norm.parent_path();   // "samples/x/" names x, not ""
  const std::string sample_name = norm.filename().string();
  std::unique_ptr<std::ofstream> csv;
  if (!frames_csv.empty()) {
    csv = std::make_unique<std::ofstream>(frames_csv, std::ios::binary);
    *csv << "trace,frame,t_ms";
    for (const auto& ip : l.profile.ips()) *csv << ',' << ip.id;
    *csv << '\n';
  }
  std::printf("%s (%s): schematic media through SchematicDetector -> debounce -> interpreter\n",
              sample_name.c_str(), l.profile.name().c_str());
  int videos = 0, passed = 0, images = 0;
  bool frames_ok = true;
  for (const auto& e : fs::directory_iterator(sample / "traces")) {
    if (e.path().extension() != ".yaml") continue;
    const std::string name = e.path().stem().string();
    if (which != "all" && which != name) continue;
    const fs::path video = sample / "media" / (name + ".mp4");
    if (!fs::exists(video)) continue;
    ++videos;
    passed += verify_video(l, video, e.path(), csv.get()) ? 1 : 0;
    frames_ok = verify_keyframes(l, sample / "media" / name, images) && frames_ok;
  }
  std::printf("  videos %d/%d PASS · keyframe images %d %s\n", passed, videos, images, frames_ok ? "all read as expected" : "MISREAD");
  return videos > 0 && passed == videos && frames_ok ? 0 : 1;
}

int run_classify_image(const fs::path& sample, const fs::path& image) {
  Loaded l = load(sample);
  const cv::Mat img = cv::imread(image.string(), cv::IMREAD_COLOR);
  if (img.empty()) {
    std::fprintf(stderr, "cannot read %s\n", image.string().c_str());
    return 1;
  }
  auto sv = std::make_unique<perception::StateVector>();
  l.det.classify(img.data, img.cols, img.rows, static_cast<int>(img.step), *sv);
  for (std::uint16_t i = 0; i < sv->n_ips; ++i)
    std::printf("%-14s %-10s conf %.2f%s\n", l.profile.ips()[i].id.c_str(), state_name(l.profile, i, sv->ips[i].rank).c_str(),
                sv->ips[i].conf, (sv->ips[i].flags & perception::kOccluded) ? "  (occluded)" : "");
  return 0;
}

#else

int run_verify_media(const fs::path&, const std::string&, const fs::path&) {
  std::fprintf(stderr, "this build has no OpenCV (headless): media verification needs the Windows build\n");
  return 2;
}
int run_classify_image(const fs::path&, const fs::path&) { return run_verify_media({}, {}, {}); }

#endif

}  // namespace sunverify::app
