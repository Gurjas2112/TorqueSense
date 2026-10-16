// The pure arithmetic of the GPU detector — letterbox geometry and NMS — kept out of OnnxDetector.cpp
// so it builds and is tested everywhere, with or without ONNX Runtime.
#include <algorithm>

#include "sunverify/perception/OnnxDetector.hpp"

namespace sunverify::perception {

Letterbox letterbox_for(int frame_w, int frame_h, int side) noexcept {
  Letterbox lb;
  lb.scale = std::min(static_cast<float>(side) / static_cast<float>(frame_w), static_cast<float>(side) / static_cast<float>(frame_h));
  lb.pad_x = (static_cast<float>(side) - static_cast<float>(frame_w) * lb.scale) / 2.0f;
  lb.pad_y = (static_cast<float>(side) - static_cast<float>(frame_h) * lb.scale) / 2.0f;
  return lb;
}

namespace {
float iou(const Box& a, const Box& b) noexcept {
  const float ix = std::max(0.0f, std::min(a.x1, b.x1) - std::max(a.x0, b.x0));
  const float iy = std::max(0.0f, std::min(a.y1, b.y1) - std::max(a.y0, b.y0));
  const float inter = ix * iy;
  const float uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b.x1 - b.x0) * (b.y1 - b.y0) - inter;
  return uni > 0 ? inter / uni : 0.0f;
}
}  // namespace

int nms(Box* boxes, int n, float iou_max, Box* keep, int max_keep) noexcept {
  std::sort(boxes, boxes + n, [](const Box& a, const Box& b) { return a.score > b.score; });   // in place: no allocation
  int k = 0;
  for (int i = 0; i < n && k < max_keep; ++i) {
    bool suppressed = false;
    for (int j = 0; j < k; ++j)
      if (keep[j].cls == boxes[i].cls && iou(keep[j], boxes[i]) > iou_max) {
        suppressed = true;
        break;
      }
    if (!suppressed) keep[k++] = boxes[i];
  }
  return k;
}

}  // namespace sunverify::perception
