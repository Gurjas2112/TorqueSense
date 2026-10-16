#pragma once
// Violations as the reference spells them (reference/tsref/procedure.py's Violation.detail), and as
// one sentence a person reads (the narrator). Both build strings, so both run off the hot path: the
// io thread, the conformance runner, the API.
#include <string>

#include <nlohmann/json.hpp>

#include "sunverify/procedure/ProcedureEngine.hpp"

namespace sunverify::procedure {

// {"kind", "step", "t_ms", and the kind's own keys: ip, now, before, observed, expected, signal, missing}
nlohmann::json describe(const Profile& p, const Violation& v);

// "bolt-2" -> "Bolt 2"; "clip-5" -> "Clip 5"; "panel-1" -> "Panel". Generic: no part is named here.
std::string display_name(const std::string& ip_id, bool capital = true);

// One deterministic sentence from the state machine's own facts (spec Section 23: "a fact, not a guess").
std::string narrate(const Profile& p, const Violation& v);

// The order still to do in a sequence step after a violation, for the HMI's corrective arc.
std::vector<std::string> corrective_order(const Profile& p, const ProcedureEngine& e, int step);

}  // namespace sunverify::procedure
