#include "sunverify/procedure/XlsxReporter.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#ifdef SUNVERIFY_WITH_XLSX
#include <OpenXLSX.hpp>
#endif

namespace sunverify::procedure {

#ifdef SUNVERIFY_WITH_XLSX
bool xlsx_supported() noexcept { return true; }

void write_shift_report(const ShiftReport& r, const std::filesystem::path& out) {
  OpenXLSX::XLDocument doc;
  doc.create(out.string(), OpenXLSX::XLForceOverwrite);
  auto wb = doc.workbook();
  auto sum = wb.worksheet("Sheet1");
  sum.setName("Summary");
  std::size_t passed = 0;
  for (const auto& c : r.cycles) passed += c.passed ? 1 : 0;
  std::map<std::string, int> by_step;
  for (const auto& a : r.anomalies) ++by_step[a.step + " · " + a.violation];
  std::vector<std::pair<std::string, int>> top(by_step.begin(), by_step.end());
  std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

  sum.cell("A1").value() = "TorqueSense shift report";
  sum.cell("A2").value() = "Station";
  sum.cell("B2").value() = r.station;
  sum.cell("A3").value() = "Profile";
  sum.cell("B3").value() = r.profile + " (" + r.profile_version + ")";
  sum.cell("A4").value() = "Generated";
  sum.cell("B4").value() = r.generated;
  sum.cell("A5").value() = "Cycles";
  sum.cell("B5").value() = static_cast<std::int64_t>(r.cycles.size());
  sum.cell("A6").value() = "First-time-right";
  sum.cell("B6").value() = r.cycles.empty() ? 0.0 : static_cast<double>(passed) / static_cast<double>(r.cycles.size());
  sum.cell("A7").value() = "Anomalies";
  sum.cell("B7").value() = static_cast<std::int64_t>(r.anomalies.size());
  sum.cell("A9").value() = "Most-violated steps";
  for (std::size_t i = 0; i < top.size() && i < 10; ++i) {
    sum.cell(static_cast<std::uint32_t>(10 + i), 1).value() = top[i].first;
    sum.cell(static_cast<std::uint32_t>(10 + i), 2).value() = static_cast<std::int64_t>(top[i].second);
  }

  wb.addWorksheet("Cycles");
  auto cy = wb.worksheet("Cycles");
  const char* ch[] = {"cycle_id", "ts", "scenario", "passed", "duration_s"};
  for (int i = 0; i < 5; ++i) cy.cell(1, static_cast<std::uint16_t>(i + 1)).value() = ch[i];
  for (std::size_t i = 0; i < r.cycles.size(); ++i) {
    const auto row = static_cast<std::uint32_t>(i + 2);
    const auto& c = r.cycles[i];
    cy.cell(row, 1).value() = c.cycle_id;
    cy.cell(row, 2).value() = c.ts;
    cy.cell(row, 3).value() = c.scenario;
    cy.cell(row, 4).value() = c.passed;
    cy.cell(row, 5).value() = c.duration_s;
  }

  wb.addWorksheet("Anomalies");
  auto an = wb.worksheet("Anomalies");
  const char* ah[] = {"ts", "cycle_id", "step", "violation", "observed", "expected", "engine_ms", "evidence_ms", "narrative"};
  for (int i = 0; i < 9; ++i) an.cell(1, static_cast<std::uint16_t>(i + 1)).value() = ah[i];
  for (std::size_t i = 0; i < r.anomalies.size(); ++i) {
    const auto row = static_cast<std::uint32_t>(i + 2);
    const auto& a = r.anomalies[i];
    an.cell(row, 1).value() = a.ts;
    an.cell(row, 2).value() = a.cycle_id;
    an.cell(row, 3).value() = a.step;
    an.cell(row, 4).value() = a.violation;
    an.cell(row, 5).value() = a.observed;
    an.cell(row, 6).value() = a.expected;
    an.cell(row, 7).value() = a.engine_ms;
    an.cell(row, 8).value() = a.evidence_ms;
    an.cell(row, 9).value() = a.narrative;
  }
  doc.save();
  doc.close();
}
#else
bool xlsx_supported() noexcept { return false; }
void write_shift_report(const ShiftReport&, const std::filesystem::path&) {
  throw std::runtime_error("this build has no OpenXLSX (headless): the Excel report is Windows-only");
}
#endif

}  // namespace sunverify::procedure
