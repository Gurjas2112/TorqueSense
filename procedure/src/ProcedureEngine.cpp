#include "sunverify/procedure/ProcedureEngine.hpp"

// Each method mirrors its namesake in reference/tsref/procedure.py, statement for statement, so a
// reviewer can read the two side by side. Where the reference grows a Python list, this writes into
// a fixed array sized at construction; nothing here allocates or locks (I1, I2).

namespace sunverify::procedure {

const char* to_string(ViolationKind k) noexcept {
  switch (k) {
    case ViolationKind::kSequence: return "SEQUENCE";
    case ViolationKind::kPrecedence: return "PRECEDENCE";
    case ViolationKind::kEvidenceMissing: return "EVIDENCE_MISSING";
    case ViolationKind::kRegression: return "REGRESSION";
    case ViolationKind::kStepTimeout: return "STEP_TIMEOUT";
    case ViolationKind::kIncompleteAtRelease: return "INCOMPLETE_AT_RELEASE";
  }
  return "?";
}

ProcedureEngine::ProcedureEngine(const Profile& profile) : profile_(&profile) {}

void ProcedureEngine::start(std::int64_t t_ms) noexcept {
  for (auto& r : runtime_) r = StepRuntime{};
  n_pending_ = n_tags_ = n_violations_ = dropped_violations_ = 0;
  const auto& steps = profile_->steps();
  for (std::size_t s = 0; s < steps.size(); ++s)
    if (steps[s].requires_.empty()) runtime_[s].activated_ms = t_ms;
}

void ProcedureEngine::flag(const Violation& v) noexcept {
  if (n_violations_ < violations_.size()) violations_[n_violations_++] = v;
  else ++dropped_violations_;
}

std::uint64_t ProcedureEngine::missing_mask(int step) const noexcept {
  return profile_->steps()[step].all_targets_mask & ~runtime_[step].done_mask;
}

void ProcedureEngine::maybe_activate(std::int64_t t_ms) noexcept {
  const auto& steps = profile_->steps();
  for (std::size_t s = 0; s < steps.size(); ++s) {
    if (runtime_[s].activated_ms >= 0) continue;
    bool ready = true;
    for (const auto r : steps[s].requires_) ready = ready && complete(r);
    if (ready) runtime_[s].activated_ms = t_ms;
  }
}

void ProcedureEngine::on_tag(std::int64_t t_ms, int tag, const TagValue& value) noexcept {
  tags_[n_tags_ % tags_.size()] = {t_ms, tag, value};
  ++n_tags_;
  if (tag < 0) return;
  // a matching signal settles every transition still waiting for it
  std::size_t keep = 0;
  for (std::size_t i = 0; i < n_pending_; ++i) {
    const Step& s = profile_->steps()[pending_[i].step];
    const bool settles = s.evidence.tag == tag && value == s.evidence.equals;
    if (!settles) pending_[keep++] = pending_[i];
  }
  n_pending_ = keep;
}

void ProcedureEngine::on_state(std::int64_t t_ms, int ip, int rank) noexcept {
  if (ip < 0 || rank < 0) return;
  std::size_t n = 0;
  const TargetRef* refs = profile_->targets_of(ip, n);
  for (std::size_t i = 0; i < n; ++i) {
    const TargetRef& ref = refs[i];
    StepRuntime& rt = runtime_[ref.step];
    const Step& st = profile_->steps()[ref.step];
    const std::uint64_t bit = 1ULL << ref.pos;
    const bool done = (rt.done_mask & bit) != 0;
    if (done && rank < ref.need) {                              // moved back below the step's state
      Violation v;
      v.t_ms = t_ms;
      v.kind = ViolationKind::kRegression;
      v.step = ref.step;
      v.ip = static_cast<std::int16_t>(ip);
      v.now_rank = static_cast<std::int8_t>(rank);
      flag(v);
      if (st.kind == StepKind::kSequence) {                     // a loosened bolt: redo the pattern
        rt.done_n = 0;
        rt.done_mask = 0;
      } else {
        std::uint8_t keep = 0;
        for (std::uint8_t k = 0; k < rt.done_n; ++k)
          if (rt.done_order[k] != ref.pos) rt.done_order[keep++] = rt.done_order[k];
        rt.done_n = keep;
        rt.done_mask &= ~bit;
      }
      rt.completed_ms = -1;
    } else if (!done && rank >= ref.need) {                     // reached, or skipped past, the step's state
      advance(t_ms, ref.step, ref.pos, ip);
    }
  }
}

void ProcedureEngine::advance(std::int64_t t_ms, int step, int pos, int ip) noexcept {
  StepRuntime& rt = runtime_[step];
  const Step& st = profile_->steps()[step];
  if (rt.activated_ms < 0) {
    Violation v;
    v.t_ms = t_ms;
    v.kind = ViolationKind::kPrecedence;
    v.step = static_cast<std::int16_t>(step);
    v.ip = static_cast<std::int16_t>(ip);
    for (const auto r : st.requires_)
      if (!complete(r)) v.before |= 1ULL << r;
    flag(v);
  } else if (st.kind == StepKind::kSequence) {
    const int expected = st.targets[rt.done_n];
    if (ip != expected) {
      Violation v;
      v.t_ms = t_ms;
      v.kind = ViolationKind::kSequence;
      v.step = static_cast<std::int16_t>(step);
      v.ip = static_cast<std::int16_t>(ip);
      v.expected = static_cast<std::int16_t>(expected);
      flag(v);
    }
  }
  rt.done_order[rt.done_n++] = static_cast<std::int16_t>(pos);
  rt.done_mask |= 1ULL << pos;
  if (st.has_evidence) {
    const std::int64_t w = st.evidence.within_ms;
    bool seen = false;
    const std::size_t have = n_tags_ < tags_.size() ? n_tags_ : tags_.size();
    for (std::size_t i = 0; i < have && !seen; ++i) {
      const TagEvent& g = tags_[i];
      seen = g.tag == st.evidence.tag && g.value == st.evidence.equals && t_ms - w <= g.t_ms && g.t_ms <= t_ms;
    }
    if (!seen && n_pending_ < pending_.size())
      pending_[n_pending_++] = {t_ms + w, static_cast<std::int16_t>(step), static_cast<std::int16_t>(ip)};
  }
  if (rt.done_mask == st.all_targets_mask && rt.completed_ms < 0) {
    rt.completed_ms = t_ms;
    maybe_activate(t_ms);
  }
}

void ProcedureEngine::tick(std::int64_t t_ms) noexcept {
  std::size_t keep = 0;
  for (std::size_t i = 0; i < n_pending_; ++i) {
    const Pending& p = pending_[i];
    if (p.deadline_ms < t_ms) {
      Violation v;
      v.t_ms = t_ms;
      v.kind = ViolationKind::kEvidenceMissing;
      v.step = p.step;
      v.ip = p.ip;
      flag(v);
    } else {
      pending_[keep++] = p;
    }
  }
  n_pending_ = keep;

  const auto& steps = profile_->steps();
  for (std::size_t s = 0; s < steps.size(); ++s) {
    StepRuntime& rt = runtime_[s];
    if (steps[s].max_duration_ms > 0 && rt.activated_ms >= 0 && !complete(static_cast<int>(s)) && !rt.timed_out &&
        t_ms - rt.activated_ms > steps[s].max_duration_ms) {
      rt.timed_out = true;
      Violation v;
      v.t_ms = t_ms;
      v.kind = ViolationKind::kStepTimeout;
      v.step = static_cast<std::int16_t>(s);
      v.missing = missing_mask(static_cast<int>(s));
      flag(v);
    }
  }
}

bool ProcedureEngine::release(std::int64_t t_ms) noexcept {
  tick(t_ms);
  bool ok = true;
  const auto& steps = profile_->steps();
  for (std::size_t s = 0; s < steps.size(); ++s) {
    if (complete(static_cast<int>(s))) continue;
    ok = false;
    Violation v;
    v.t_ms = t_ms;
    v.kind = ViolationKind::kIncompleteAtRelease;
    v.step = static_cast<std::int16_t>(s);
    v.missing = missing_mask(static_cast<int>(s));
    flag(v);
  }
  for (std::size_t i = 0; i < n_violations_; ++i) {
    const auto k = violations_[i].kind;
    if (k == ViolationKind::kSequence || k == ViolationKind::kPrecedence || k == ViolationKind::kEvidenceMissing)
      ok = false;
  }
  return ok;
}

StepStatus ProcedureEngine::step_status(int step) const noexcept {
  if (complete(step)) return StepStatus::kComplete;
  return runtime_[step].activated_ms >= 0 ? StepStatus::kActive : StepStatus::kWaiting;
}

bool ProcedureEngine::all_complete() const noexcept {
  for (std::size_t s = 0; s < profile_->steps().size(); ++s)
    if (!complete(static_cast<int>(s))) return false;
  return true;
}

int ProcedureEngine::expected_next(int step) const noexcept {
  const Step& st = profile_->steps()[step];
  const StepRuntime& rt = runtime_[step];
  if (rt.done_n >= st.targets.size()) return -1;
  if (st.kind == StepKind::kSequence) return st.targets[rt.done_n];
  for (std::size_t k = 0; k < st.targets.size(); ++k)
    if (!(rt.done_mask & (1ULL << k))) return st.targets[k];
  return -1;
}

}  // namespace sunverify::procedure
