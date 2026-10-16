#pragma once
// Geometry from the STEP file (spec Section 7): a depth-first traversal of the assembly tree through
// OCCT's XCAF, every leaf occurrence with its accumulated transform, joined to the manifest by part
// number; and the occlusion test — a ray from each inspection point's camera to the point,
// intersected with the assembly's other bodies (IntCurvesFace_ShapeIntersector).
//
// Built only when OCCT is found (SUNVERIFY_WITH_OCCT). Without it, a profile with geometry: cad and
// an assembly.step is refused rather than compiled from the manifest's placeholder positions.
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "sunverify/cad/Optics.hpp"

namespace sunverify::cad {

struct Manifest;
struct Compiled;

struct Occurrence {
  std::string part_number;   // the name of the part (the referred label), e.g. BLT-M12-1.25-45
  std::string path;          // instance path in the tree, for messages
  Vec3 position_mm{};        // the occurrence's origin in the assembly frame: the inspection point
  Vec3 normal{0, 0, 1};      // the occurrence's +Z in the assembly frame: the feature's surface normal
  std::shared_ptr<void> shape;   // the located TopoDS_Shape (opaque here, so OCCT stays out of this header)
};

bool step_support_built() noexcept;

// Every leaf occurrence of the STEP assembly, in traversal order.
std::vector<Occurrence> read_step_occurrences(const std::filesystem::path& step);

// Replace each group's member positions with its part number's occurrences, in traversal order.
// A group that names a part number found nowhere in the STEP file, or a different count from the
// members the manifest lists, is a compile error: the manifest and the CAD disagree.
void join_by_part_number(Manifest& m, const std::vector<Occurrence>& occurrences);

// Ray-cast every inspection point against the other bodies; occluded points become unresolved (I17).
void apply_occlusion(const Manifest& m, const std::vector<Occurrence>& occurrences, Compiled& c);

}  // namespace sunverify::cad
