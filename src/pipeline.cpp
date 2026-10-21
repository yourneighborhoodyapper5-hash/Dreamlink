#include "pipeline.hpp"

namespace dreamlink {

void SessionPolicy::registerSession(const std::string& session_id,
                                    std::set<std::string> allowed_sources) {
  sessions_[session_id] = std::move(allowed_sources);
}

Error SessionPolicy::authorize(const Event& e) const {
  auto it = sessions_.find(e.session_id);
  if (it == sessions_.end()) return {ErrorCode::SessionForbidden, "unknown session"};
  if (!it->second.empty()) {
    if (!e.source_id || !it->second.count(*e.source_id))
      return {ErrorCode::SessionForbidden, "source is not permitted for this session"};
  }
  return {};
}

void SessionRouter::subscribe(const std::string& session_id, Delivery fn) {
  subscribers_[session_id].push_back(std::move(fn));
}

size_t SessionRouter::deliver(const Event& e, Disposition d) {
  auto it = subscribers_.find(e.session_id);
  if (it == subscribers_.end()) return 0;
  size_t n = 0;
  for (auto& fn : it->second) {
    try {
      fn(e, d);
      ++n;
    } catch (...) {
    }
  }
  return n;
}

Disposition classify(const Event& e, double action_threshold) {
  if (e.confidence < action_threshold) return Disposition::LoggedOnly;
  if (e.type == EventType::LogoffIntent) return Disposition::RequiresConfirmation;
  return Disposition::Actionable;
}

size_t Stats::totalRejected() const {
  size_t n = 0;
  for (const auto& kv : rejected) n += kv.second;
  return n;
}

Pipeline::Pipeline(PipelineConfig cfg, SessionPolicy policy, EventStore& store,
                   SessionRouter& router, Clock clock)
    : cfg_(std::move(cfg)),
      policy_(std::move(policy)),
      store_(store),
      router_(router),
      clock_(std::move(clock)) {}

Outcome Pipeline::reject(const Error& e, const std::string& id) {
  ++stats_.rejected[e.code];
  Outcome o;
  o.status = Outcome::Status::Rejected;
  o.error = e;
  o.event_id = id;
  return o;
}

Outcome Pipeline::submit(const std::string& raw) {
  ++stats_.submitted;

  DecodeResult d = decodeEvent(raw, cfg_.limits);
  if (!d.ok()) return reject(d.error);
  const Event& ev = d.event;

  TimePoint now = clock_();
  if (Error e = checkTimestampWindow(ev, now, cfg_.limits); !e.ok()) return reject(e, ev.id);
  if (Error e = policy_.authorize(ev); !e.ok()) return reject(e, ev.id);

  LogRecord rec;
  rec.event = ev;
  rec.disposition = classify(ev, cfg_.action_threshold);
  rec.received_at = truncateToMillis(now);

  // Durable log first; routing only happens for events that were newly stored.
  // A duplicate is acknowledged but never re-processed.
  std::string err;
  Outcome o;
  o.event_id = ev.id;
  switch (store_.append(rec, err)) {
    case AppendResult::Failed:
      ++stats_.storage_errors;
      o.status = Outcome::Status::StorageError;
      o.error = {ErrorCode::StorageFailure, err};
      return o;
    case AppendResult::Duplicate:
      ++stats_.duplicates;
      o.status = Outcome::Status::Duplicate;
      return o;
    case AppendResult::Inserted:
      break;
  }

  ++stats_.accepted;
  o.status = Outcome::Status::Accepted;
  o.disposition = rec.disposition;
  switch (rec.disposition) {
    case Disposition::Actionable: ++stats_.actionable; break;
    case Disposition::RequiresConfirmation: ++stats_.requires_confirmation; break;
    case Disposition::LoggedOnly: ++stats_.logged_only; break;
  }
  if (rec.disposition != Disposition::LoggedOnly)
    stats_.deliveries += router_.deliver(ev, rec.disposition);
  return o;
}

}  // namespace dreamlink
