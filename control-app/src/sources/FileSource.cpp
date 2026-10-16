// FileSource (spec Section 8): rendered scenarios and recordings through OpenCV's FFmpeg backend.
// A capture source: allowed platform differences, though none are needed here.
#include <cstring>
#include <filesystem>

#include "sources/FrameSource.hpp"

#ifdef SUNVERIFY_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace sunverify::app {
namespace {

#ifdef SUNVERIFY_WITH_OPENCV
class FileSource final : public IFrameSource {
 public:
  explicit FileSource(std::string dir) : dir_(std::move(dir)) { jpeg_.reserve(512 * 1024); }

  bool open(const std::string& scenario) override {
    cap_.release();
    path_ = (std::filesystem::path(dir_) / (scenario + ".mp4")).string();
    have_video_ = std::filesystem::exists(path_) && cap_.open(path_, cv::CAP_FFMPEG);
    return true;   // no video: the cycle still runs, on blank frames (the replay detector does not need pixels)
  }

  bool read(std::uint8_t* dst, int w, int h) override {
    cv::Mat out(h, w, CV_8UC3, dst);                 // wraps the pool frame: resize writes in place
    if (have_video_ && cap_.read(frame_)) {
      if (frame_.cols == w && frame_.rows == h) frame_.copyTo(out);
      else cv::resize(frame_, out, out.size(), 0, 0, cv::INTER_AREA);
    } else {
      out.setTo(cv::Scalar(30, 26, 23));
    }
    last_ = out;
    return true;
  }

  const std::vector<std::uint8_t>& jpeg(int quality) override {
    jpeg_.clear();
    if (!last_.empty()) cv::imencode(".jpg", last_, jpeg_, {cv::IMWRITE_JPEG_QUALITY, quality});
    return jpeg_;
  }

  std::string describe() const override {
    return have_video_ ? "file " + path_ + " (FFmpeg)" : "file " + path_ + " missing: blank frames";
  }

 private:
  std::string dir_, path_;
  cv::VideoCapture cap_;
  cv::Mat frame_, last_;
  bool have_video_ = false;
  std::vector<std::uint8_t> jpeg_;
};
#endif

class BlankSource final : public IFrameSource {
 public:
  bool open(const std::string&) override { return true; }
  bool read(std::uint8_t* dst, int w, int h) override {
    std::memset(dst, 24, static_cast<std::size_t>(w) * h * 3);
    return true;
  }
  const std::vector<std::uint8_t>& jpeg(int) override { return empty_; }
  std::string describe() const override { return "blank (no OpenCV in this build)"; }

 private:
  std::vector<std::uint8_t> empty_;
};

}  // namespace

std::unique_ptr<IFrameSource> make_file_source(const std::string& dir) {
#ifdef SUNVERIFY_WITH_OPENCV
  return std::make_unique<FileSource>(dir);
#else
  (void)dir;
  return std::make_unique<BlankSource>();
#endif
}

}  // namespace sunverify::app
