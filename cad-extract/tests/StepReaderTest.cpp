// The STEP half of the compiler, on a real STEP file: an assembly is built with OCCT's XCAF — a hub
// and five instances of one bolt on a 114.3 mm PCD — written with STEPCAFControl_Writer, then read
// back through the same path a FreeCAD export takes. Then the occlusion ray-cast, with a cover plate
// placed over one bolt.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <numbers>

#include "sunverify/cad/Compiler.hpp"
#include "sunverify/cad/StepReader.hpp"

#ifdef SUNVERIFY_WITH_OCCT
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#endif

using namespace sunverify::cad;
namespace fs = std::filesystem;

#ifdef SUNVERIFY_WITH_OCCT
namespace {

constexpr double kPcdRadius = 114.3 / 2;

// Writes the test assembly. with_cover adds a plate between the camera and bolt 1.
fs::path write_wheel_step(const fs::path& dir, bool with_cover) {
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);
  Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

  const TDF_Label assy = tool->NewShape();
  TDataStd_Name::Set(assy, "RL_WHEEL_ASSY_REV_C");

  const TopoDS_Shape hub = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -30), gp::DZ()), 80, 20).Shape();
  const TDF_Label hub_l = tool->AddShape(hub, false);
  TDataStd_Name::Set(hub_l, "HUB-114.3");
  tool->AddComponent(assy, hub_l, TopLoc_Location());

  // the bolt's own origin is the centre of its head face, +Z out of the head: the inspection point
  const TopoDS_Shape bolt = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -10), gp::DZ()), 8.5, 10).Shape();
  const TDF_Label bolt_l = tool->AddShape(bolt, false);
  TDataStd_Name::Set(bolt_l, "BLT-M12-1.25-45");
  for (int i = 0; i < 5; ++i) {
    const double a = (90.0 - 72.0 * i) * std::numbers::pi / 180.0;
    gp_Trsf t;
    t.SetTranslation(gp_Vec(kPcdRadius * std::cos(a), kPcdRadius * std::sin(a), 0));
    tool->AddComponent(assy, bolt_l, TopLoc_Location(t));
  }
  if (with_cover) {
    const TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(-15, 40, 200), 30, 30, 5).Shape();   // over bolt 1
    const TDF_Label plate_l = tool->AddShape(plate, false);
    TDataStd_Name::Set(plate_l, "COVER-PLATE");
    tool->AddComponent(assy, plate_l, TopLoc_Location());
  }
  tool->UpdateAssemblies();

  const fs::path out = dir / (with_cover ? "wheel_cover.step" : "wheel.step");
  STEPCAFControl_Writer w;
  w.SetNameMode(true);
  EXPECT_TRUE(w.Transfer(doc, STEPControl_AsIs));
  EXPECT_EQ(w.Write(out.string().c_str()), IFSelect_RetDone);
  app->Close(doc);
  return out;
}

const char* kManifest = R"(
profile: wheel-from-step
geometry: cad
cameras:
  - {id: cam-1, width_px: 1920, hfov_deg: 70.4, position_mm: [0, 0, 900], look_at_mm: [0, 0, 0]}
groups:
  - id: bolt
    family: threaded_fastener
    states: [absent, seated, fastened]
    part_number: BLT-M12-1.25-45
    feature_mm: 17
    members: []
steps:
  - {id: torque, group: bolt, kind: sequence, order: derive, state: fastened, max_duration_s: 16}
)";

}  // namespace

TEST(StepReader, ReadsEveryOccurrenceWithItsTransform) {
  const fs::path dir = fs::temp_directory_path() / "ts_step_test";
  fs::create_directories(dir);
  const auto occ = read_step_occurrences(write_wheel_step(dir, false));
  int bolts = 0;
  for (const auto& o : occ) {
    if (o.part_number != "BLT-M12-1.25-45") continue;
    const double a = (90.0 - 72.0 * bolts) * std::numbers::pi / 180.0;
    EXPECT_NEAR(o.position_mm[0], kPcdRadius * std::cos(a), 1e-6);
    EXPECT_NEAR(o.position_mm[1], kPcdRadius * std::sin(a), 1e-6);
    EXPECT_NEAR(o.position_mm[2], 0.0, 1e-6);
    EXPECT_NEAR(o.normal[2], 1.0, 1e-9);
    ++bolts;
  }
  EXPECT_EQ(bolts, 5);
}

TEST(StepReader, JoinsByPartNumberAndDerivesTheStarFromCad) {
  const fs::path dir = fs::temp_directory_path() / "ts_step_test";
  fs::create_directories(dir);
  Manifest m = parse_manifest(kManifest);
  const auto occ = read_step_occurrences(write_wheel_step(dir, false));
  join_by_part_number(m, occ);
  Compiled c = compile(m);
  apply_occlusion(m, occ, c);
  EXPECT_EQ(c.procedure["steps"][0]["targets"],
            (std::vector<std::string>{"bolt-1", "bolt-3", "bolt-5", "bolt-2", "bolt-4"}));
  EXPECT_TRUE(c.ok());
  for (const auto& p : c.coverage["points"]) EXPECT_EQ(p["occlusion"], "clear") << p["ip"];
}

TEST(StepReader, AnOccludedBoltIsUnresolved) {   // I17: unoccluded, as well as large enough
  const fs::path dir = fs::temp_directory_path() / "ts_step_test";
  fs::create_directories(dir);
  Manifest m = parse_manifest(kManifest);
  const auto occ = read_step_occurrences(write_wheel_step(dir, true));
  join_by_part_number(m, occ);
  Compiled c = compile(m);
  apply_occlusion(m, occ, c);
  EXPECT_FALSE(c.ok());
  EXPECT_EQ(c.unresolved, (std::vector<std::string>{"bolt-1"}));
  EXPECT_EQ(c.coverage["points"][0]["occlusion"], "occluded:COVER-PLATE");
}

TEST(StepReader, AMissingPartNumberIsACompileError) {
  const fs::path dir = fs::temp_directory_path() / "ts_step_test";
  fs::create_directories(dir);
  std::string y = kManifest;
  y.replace(y.find("BLT-M12-1.25-45"), 15, "BLT-M14-1.50-50");
  Manifest m = parse_manifest(y);
  EXPECT_THROW(join_by_part_number(m, read_step_occurrences(write_wheel_step(dir, false))), CompileError);
}
#else
TEST(StepReader, NotBuiltWithoutOcct) { EXPECT_FALSE(step_support_built()); }
#endif
