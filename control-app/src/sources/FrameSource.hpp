#pragma once
// IFrameSource — the only way frames enter the engine (spec Section 8). Everything downstream of the
// capture thread is the same whatever the camera. FileSource plays a scenario's video (rendered, or
// the schematic stand-in); UvcSource and StreamSource (laptop webcam, phone) are Phase 11.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sunverify::app {

class IFrameSource {
 public:
  virtual ~IFrameSource() = default;
  // Opens the next cycle's input (a scenario name for a file source). False: nothing to play.
  virtual bool open(const std::string& scenario) = 0;
  // Writes the next frame, resized to w x h BGR, into dst. False at the end of the input.
  virtual bool read(std::uint8_t* dst, int w, int h) = 0;
  // JPEG of the last frame read, for the clip ring and the MJPEG stream. Empty if unsupported.
  virtual const std::vector<std::uint8_t>& jpeg(int quality) = 0;
  virtual std::string describe() const = 0;
};

// FileSource when OpenCV is built in (scenarios_dir/<scenario>.mp4), else a source that produces
// blank frames at the right size, so a headless build still paces the pipeline.
std::unique_ptr<IFrameSource> make_file_source(const std::string& scenarios_dir);

}  // namespace sunverify::app
