#include "sunverify/cad/StepReader.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "sunverify/cad/Compiler.hpp"

#ifdef SUNVERIFY_WITH_OCCT
#include <IFSelect_ReturnStatus.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#endif

namespace sunverify::cad {

#ifdef SUNVERIFY_WITH_OCCT
namespace {

std::string label_name(const TDF_Label& l) {
  Handle(TDataStd_Name) n;
  if (!l.FindAttribute(TDataStd_Name::GetID(), n)) return {};
  const TCollection_ExtendedString& e = n->Get();
  std::string out;
  out.reserve(static_cast<std::size_t>(e.Length()));
  for (int i = 1; i <= e.Length(); ++i) {
    const auto ch = e.Value(i);
    out.push_back(ch < 128 ? static_cast<char>(ch) : '?');
  }
  return out;
}

// Depth-first over the assembly tree. A component is a reference with its own location; locations
// compose from the root down, so every leaf ends up in the assembly frame.
void visit(const Handle(XCAFDoc_ShapeTool) & tool, const TDF_Label& label, const TopLoc_Location& loc,
           const std::string& path, std::vector<Occurrence>& out) {
  if (XCAFDoc_ShapeTool::IsAssembly(label)) {
    TDF_LabelSequence comps;
    XCAFDoc_ShapeTool::GetComponents(label, comps);
    for (int i = 1; i <= comps.Length(); ++i) {
      const TDF_Label comp = comps.Value(i);
      TDF_Label ref;
      if (!XCAFDoc_ShapeTool::GetReferredShape(comp, ref)) continue;
      const TopLoc_Location here = loc * XCAFDoc_ShapeTool::GetLocation(comp);
      std::string name = label_name(comp);
      if (name.empty()) name = label_name(ref);
      visit(tool, ref, here, path + "/" + name, out);
    }
    return;
  }
  TopoDS_Shape shape = XCAFDoc_ShapeTool::GetShape(label);
  if (shape.IsNull()) return;
  const gp_Trsf t = loc.Transformation();
  const gp_Pnt o = gp_Pnt(0, 0, 0).Transformed(t);
  const gp_Dir z = gp_Dir(0, 0, 1).Transformed(t);
  Occurrence occ;
  occ.part_number = label_name(label);
  occ.path = path;
  occ.position_mm = {o.X(), o.Y(), o.Z()};
  occ.normal = {z.X(), z.Y(), z.Z()};
  occ.shape = std::make_shared<TopoDS_Shape>(shape.Located(loc));
  out.push_back(std::move(occ));
}

const TopoDS_Shape& shape_of(const Occurrence& o) { return *static_cast<const TopoDS_Shape*>(o.shape.get()); }

}  // namespace

bool step_support_built() noexcept { return true; }

std::vector<Occurrence> read_step_occurrences(const std::filesystem::path& step) {
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);
  STEPCAFControl_Reader reader;
  reader.SetNameMode(true);
  if (reader.ReadFile(step.string().c_str()) != IFSelect_RetDone)
    throw CompileError("cannot read STEP file " + step.string());
  if (!reader.Transfer(doc)) throw CompileError("cannot transfer STEP file " + step.string());
  Handle(XCAFDoc_ShapeTool) tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  TDF_LabelSequence roots;
  tool->GetFreeShapes(roots);
  std::vector<Occurrence> out;
  for (int i = 1; i <= roots.Length(); ++i) visit(tool, roots.Value(i), TopLoc_Location(), label_name(roots.Value(i)), out);
  app->Close(doc);
  return out;
}

void apply_occlusion(const Manifest& m, const std::vector<Occurrence>& occurrences, Compiled& c) {
  // which occurrence each inspection point is (groups joined by part number), so a ray that hits the
  // point's own part is not counted against it
  std::map<std::string, const Occurrence*> own;
  for (const auto& g : m.groups) {
    if (g.part_number.empty()) continue;
    int k = 0;
    for (const auto& o : occurrences)
      if (o.part_number == g.part_number) own[g.id + "-" + std::to_string(++k)] = &o;
  }
  auto camera = [&](const std::string& id) -> const Camera* {
    for (const auto& cam : m.cameras)
      if (cam.id == id) return &cam;
    return nullptr;
  };

  for (auto& row : c.rows) {
    const Camera* cam = camera(row.footprint.camera);
    const auto ip = std::find_if(c.procedure["inspection_points"].begin(), c.procedure["inspection_points"].end(),
                                 [&](const nlohmann::json& j) { return j["id"] == row.ip; });
    if (!cam || ip == c.procedure["inspection_points"].end()) continue;
    const auto p = (*ip)["position_mm"];
    const gp_Pnt target(p[0].get<double>(), p[1].get<double>(), p[2].get<double>());
    const gp_Pnt eye(cam->position_mm[0], cam->position_mm[1], cam->position_mm[2]);
    const double d = eye.Distance(target);
    if (d <= 0) continue;
    const gp_Lin ray(eye, gp_Dir(gp_Vec(eye, target)));
    const Occurrence* mine = own.count(row.ip) ? own[row.ip] : nullptr;
    std::string blocker;
    double nearest = d - 0.5;                               // a hit this close to the point is the point itself
    for (const auto& o : occurrences) {
      if (&o == mine) continue;
      IntCurvesFace_ShapeIntersector x;
      x.Load(shape_of(o), 1e-6);
      x.Perform(ray, 0.0, d);
      if (!x.IsDone()) continue;
      for (int i = 1; i <= x.NbPnt(); ++i) {
        if (x.WParameter(i) < nearest) {
          nearest = x.WParameter(i);
          blocker = o.part_number.empty() ? o.path : o.part_number;
        }
      }
    }
    row.occlusion = blocker.empty() ? "clear" : "occluded:" + blocker;
    if (!blocker.empty() && row.resolved) {
      row.resolved = false;
      c.unresolved.push_back(row.ip);
    }
  }
  for (auto& j : c.coverage["points"])
    for (const auto& row : c.rows)
      if (j["ip"] == row.ip) {
        j["occlusion"] = row.occlusion;
        j["resolved"] = row.resolved;
      }
  c.coverage["unresolved"] = c.unresolved;
  c.coverage["ok"] = c.ok();
}

#else

bool step_support_built() noexcept { return false; }

std::vector<Occurrence> read_step_occurrences(const std::filesystem::path& step) {
  throw CompileError("this build has no OCCT, so it cannot read " + step.string() +
                     " — install mingw-w64-ucrt-x86_64-opencascade (spec Section 28 step 4)");
}

void apply_occlusion(const Manifest&, const std::vector<Occurrence>&, Compiled&) {}

#endif

void join_by_part_number(Manifest& m, const std::vector<Occurrence>& occurrences) {
  for (auto& g : m.groups) {
    if (g.part_number.empty()) continue;
    std::vector<Member> found;
    for (const auto& o : occurrences)
      if (o.part_number == g.part_number) found.push_back({o.position_mm, o.normal});
    if (found.empty()) throw CompileError("group " + g.id + ": part number " + g.part_number + " is not in the STEP file");
    if (!g.members.empty() && g.members.size() != found.size())
      throw CompileError("group " + g.id + ": the manifest lists " + std::to_string(g.members.size()) +
                         " members but the STEP file has " + std::to_string(found.size()) + " of " + g.part_number);
    g.members = std::move(found);
  }
}

}  // namespace sunverify::cad
