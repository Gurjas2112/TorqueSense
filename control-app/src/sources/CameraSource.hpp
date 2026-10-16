#pragma once
// The laptop webcam (UvcSource, DirectShow) and the phone (StreamSource, MJPEG over its hotspot).
#include <memory>
#include <optional>
#include <string>

#include "sources/FrameSource.hpp"

namespace sunverify::app {

struct CameraSettings {
  std::string kind = "uvc";          // uvc | stream
  int device = 0;                    // uvc: DirectShow index (ffmpeg -list_devices true -f dshow -i dummy)
  std::string url;                   // stream: e.g. http://192.168.43.1:8080/video
  int width = 1280, height = 720;    // asked for, then read back
  double fps = 30;
  std::string fourcc = "MJPG";
  std::optional<double> exposure;
};

struct Readback {
  int width = 0, height = 0;
  double fps = 0;
  std::string fourcc;
  std::string describe() const;
  bool matches(const CameraSettings& want) const;
};

std::unique_ptr<IFrameSource> make_camera_source(const CameraSettings& s);
// torquesense --probe-camera uvc:0:1280x720@30 | stream:<url>:1920x1080 — opens, reads back, prints, closes.
int probe_camera(const CameraSettings& s);

}  // namespace sunverify::app
