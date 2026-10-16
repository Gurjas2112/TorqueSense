// UvcSource and StreamSource (spec Section 8): the laptop webcam over DirectShow and the phone's MJPEG
// stream over its own hotspot. Settings are configuration, not defaults: the source asks for a size,
// format and rate, then READS THEM BACK, and a mismatch refuses to verify — a webcam opened without
// asking quietly delivers 640 x 480 and halves every pixel count, and nothing else would warn.
// A capture source: platform #ifdefs are allowed here (coding instructions, "Toolchain").
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "sources/CameraSource.hpp"

#ifdef SUNVERIFY_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace sunverify::app {

std::string Readback::describe() const {
  std::ostringstream s;
  s << width << 'x' << height << '@' << fps << ' ' << fourcc;
  return s.str();
}

bool Readback::matches(const CameraSettings& want) const {
  // DirectShow reports the rate it negotiated; a camera that cannot do 30 fps at 720p shows it here
  return width == want.width && height == want.height && std::abs(fps - want.fps) < 1.0 &&
         (want.fourcc.empty() || fourcc == want.fourcc);
}

#ifdef SUNVERIFY_WITH_OPENCV
namespace {

std::string fourcc_text(double v) {
  const int c = static_cast<int>(v);
  char s[5] = {static_cast<char>(c & 0xff), static_cast<char>((c >> 8) & 0xff), static_cast<char>((c >> 16) & 0xff),
               static_cast<char>((c >> 24) & 0xff), 0};
  for (char& ch : s)
    if (ch && (ch < 32 || ch > 126)) ch = '?';
  return s;
}

class CameraSource final : public IFrameSource {
 public:
  explicit CameraSource(CameraSettings s) : s_(std::move(s)) { jpeg_.reserve(512 * 1024); }

  bool open(const std::string&) override {
    if (cap_.isOpened()) return ok_;
    if (s_.kind == "uvc") {
      cap_.open(s_.device, cv::CAP_DSHOW);                                   // DirectShow: works in a MinGW build
      cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));   // before the size: raw YUY2
      cap_.set(cv::CAP_PROP_FRAME_WIDTH, s_.width);                          // at 720p rarely reaches 30 fps
      cap_.set(cv::CAP_PROP_FRAME_HEIGHT, s_.height);
      cap_.set(cv::CAP_PROP_FPS, s_.fps);
      cap_.set(cv::CAP_PROP_AUTOFOCUS, 0);
      if (s_.exposure) cap_.set(cv::CAP_PROP_EXPOSURE, *s_.exposure);       // DirectShow units: log2 seconds
    } else {
#ifdef _WIN32
      _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "fflags;nobuffer|flags;low_delay");   // newest frame, no buffering
#else
      setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "fflags;nobuffer|flags;low_delay", 1);
#endif
      cap_.open(s_.url, cv::CAP_FFMPEG);
    }
    if (!cap_.isOpened()) {
      why_ = s_.kind == "uvc" ? "webcam did not open — Settings > Privacy & security > Camera > desktop apps"
                              : "stream did not open — is the laptop on the phone's hotspot, and the URL right?";
      return ok_ = false;
    }
    got_ = {static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH)), static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT)),
            cap_.get(cv::CAP_PROP_FPS), fourcc_text(cap_.get(cv::CAP_PROP_FOURCC))};
    if (s_.kind == "stream") {                                              // a stream reports its size once a frame arrives
      cv::Mat probe;
      if (cap_.read(probe)) got_.width = probe.cols, got_.height = probe.rows;
      CameraSettings want = s_;
      want.fourcc.clear();
      ok_ = got_.width == want.width && got_.height == want.height;
    } else {
      ok_ = got_.matches(s_);
    }
    if (!ok_) why_ = "settings mismatch: asked " + Readback{s_.width, s_.height, s_.fps, s_.fourcc}.describe() + ", got " + got_.describe();
    return ok_;
  }

  bool read(std::uint8_t* dst, int w, int h) override {
    if (!ok_ || !cap_.read(frame_)) return false;                          // a lost camera stops publishing: heartbeat stops
    cv::Mat out(h, w, CV_8UC3, dst);
    cv::resize(frame_, out, out.size(), 0, 0, cv::INTER_AREA);
    last_ = out;
    return true;
  }

  const std::vector<std::uint8_t>& jpeg(int quality) override {
    jpeg_.clear();
    if (!last_.empty()) cv::imencode(".jpg", last_, jpeg_, {cv::IMWRITE_JPEG_QUALITY, quality});
    return jpeg_;
  }

  std::string describe() const override {
    return (s_.kind == "uvc" ? "webcam " + std::to_string(s_.device) : "stream " + s_.url) + " " + got_.describe() +
           (ok_ ? "" : " — REFUSED: " + why_);
  }

  Readback readback() const { return got_; }
  bool ok() const { return ok_; }
  const std::string& why() const { return why_; }

 private:
  CameraSettings s_;
  cv::VideoCapture cap_;
  cv::Mat frame_, last_;
  std::vector<std::uint8_t> jpeg_;
  Readback got_;
  bool ok_ = false;
  std::string why_;
};

}  // namespace

std::unique_ptr<IFrameSource> make_camera_source(const CameraSettings& s) { return std::make_unique<CameraSource>(s); }

int probe_camera(const CameraSettings& s) {
  CameraSource c{s};
  const bool ok = c.open("");
  std::printf("%s\n", c.describe().c_str());
  return ok ? 0 : 1;
}

#else

std::unique_ptr<IFrameSource> make_camera_source(const CameraSettings&) { return nullptr; }
int probe_camera(const CameraSettings&) {
  std::fprintf(stderr, "this build has no OpenCV (headless): no camera sources\n");
  return 2;
}

#endif

}  // namespace sunverify::app
