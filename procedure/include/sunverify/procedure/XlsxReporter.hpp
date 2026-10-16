#pragma once
// The shift report (spec Sections 1, 21): how a quality engineer actually consumes this data on a
// line — an Excel workbook for the morning meeting. Written in C++ with OpenXLSX, off the hot path.
#include <filesystem>
#include <string>
#include <vector>

namespace sunverify::procedure {

struct ReportCycle {
  std::string cycle_id, ts, scenario;
  bool passed = false;
  double duration_s = 0;
};

struct ReportAnomaly {
  std::string ts, cycle_id, step, violation, observed, expected, narrative;
  double engine_ms = 0, evidence_ms = 0;
};

struct ShiftReport {
  std::string station, profile, profile_version, generated;
  std::vector<ReportCycle> cycles;
  std::vector<ReportAnomaly> anomalies;
};

bool xlsx_supported() noexcept;
// Writes a three-sheet workbook: Summary (first-time-right, most-violated steps), Cycles, Anomalies.
void write_shift_report(const ShiftReport& r, const std::filesystem::path& out);

}  // namespace sunverify::procedure
