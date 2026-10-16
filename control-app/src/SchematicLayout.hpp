#pragma once
// synth/schematic.py's layout(), in C++: each camera a tile across the 960 x 540 frame, every inspection
// point at its CAD position projected through its own camera, scaled into that tile. The renderer and
// the SchematicDetector must agree to the pixel, so this is the one copy the engine uses.
#include <vector>

#include <nlohmann/json.hpp>

#include "sunverify/perception/SchematicDetector.hpp"

namespace sunverify::app {

struct SchematicLayout {
  std::vector<perception::SchematicDetector::Point> pts;
  int radius = 9;
};

SchematicLayout schematic_layout(const nlohmann::json& procedure);

}  // namespace sunverify::app
