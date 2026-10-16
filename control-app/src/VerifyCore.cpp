#include "VerifyCore.hpp"

namespace sunverify::app {

VerifyCore::VerifyCore(const procedure::Profile& p, perception::CommitConfig cfg)
    : profile_(p), engine_(p), committer_(cfg) {
  committer_.reset(static_cast<std::uint16_t>(p.ips().size()));
}

void VerifyCore::stamp_new(std::int64_t first_ns, std::int64_t commit_ns) noexcept {
  const std::size_t n = engine_.violations().size();
  for (; stamped_ < n; ++stamped_) {
    first_ns_[stamped_] = first_ns;
    commit_ns_[stamped_] = commit_ns;
  }
}

void VerifyCore::on_frame(const perception::StateVector& sv) noexcept {
  if (sv.new_cycle || !started_) {
    cycle_t0_ns_ = sv.t_capture_ns;
    cycle_no_ = sv.cycle_no;
    committer_.reset(static_cast<std::uint16_t>(profile_.ips().size()));
    engine_.start(0);
    stamped_ = reported_ = 0;
    started_ = true;
    released_ = release_ok_ = false;
  }
  committer_.observe(sv, events_, n_events_, asks_, n_asks_);
  for (std::size_t i = 0; i < n_events_; ++i) {
    const auto& e = events_[i];
    engine_.on_state(cycle_ms(e.t_commit_ns), e.ip, e.rank);
    stamp_new(e.t_first_ns, e.t_commit_ns);                 // violations this commit caused
  }
  if (!released_) {
    engine_.tick(cycle_ms(sv.t_capture_ns));
    stamp_new(sv.t_capture_ns, sv.t_capture_ns);            // deadlines: the frame that noticed
  }
}

void VerifyCore::on_signal(std::int64_t t_ns, int tag, const procedure::TagValue& v) noexcept {
  if (!started_) return;
  engine_.on_tag(cycle_ms(t_ns), tag, v);
}

void VerifyCore::on_adjudication(const AdjResult& r) noexcept {
  perception::CommitEvent e;
  if (!committer_.resolve(r.ip, r.candidate, r.confirmed, r.t_ns, e)) return;
  engine_.on_state(cycle_ms(e.t_commit_ns), e.ip, e.rank);
  stamp_new(e.t_first_ns, e.t_commit_ns);
}

bool VerifyCore::on_release(std::int64_t t_ns) noexcept {
  if (!started_ || released_) return release_ok_;
  release_ok_ = engine_.release(cycle_ms(t_ns));
  released_ = true;
  stamp_new(t_ns, t_ns);
  return release_ok_;
}

}  // namespace sunverify::app
