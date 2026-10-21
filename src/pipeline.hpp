// Validation -> authorization -> classification -> durable log -> routing.
#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "codec.hpp"
#include "store.hpp"

namespace dreamlink {

// Which sessions exist and which devices may send into them.
class SessionPolicy {
 public:
  // An empty allowed_sources set means "any source may send".
  void registerSession(const std::string& session_id, std::set<std::string> allowed_sources = {});
  Error authorize(const Event& e) const;

 private:
  std::map<std::string, std::set<std::string>> sessions_;
};

// In-process stand-in for "session routing -> connected participants".
class SessionRouter {
 public:
  using Delivery = std::function<void(const Event&, Disposition)>;
  void subscribe(const std::string& session_id, Delivery fn);
  // Returns the number of subscribers that received the event. A throwing
  // subscriber is skipped so it cannot break the pipeline.
  size_t deliver(const Event& e, Disposition d);

 private:
  std::map<std::string, std::vector<Delivery>> subscribers_;
};

struct PipelineConfig {
  Limits limits;
  // Below this confidence an event is logged for research but never acted on.
  double action_threshold = 0.80;
};

// Policy: low confidence -> LoggedOnly. LOGOFF_INTENT is never Actionable by
// itself: even at high confidence it only *requests* confirmation.
Disposition classify(const Event& e, double action_threshold);

struct Outcome {
  enum class Status { Accepted, Duplicate, Rejected, StorageError };
  Status status = Status::Rejected;
  Error error;  // set for Rejected / StorageError
  Disposition disposition = Disposition::LoggedOnly;  // valid for Accepted
  std::string event_id;  // valid when the event parsed
  // Ack the sender only for Accepted or Duplicate. Retry on StorageError.
};

struct Stats {
  size_t submitted = 0, accepted = 0, duplicates = 0, storage_errors = 0, deliveries = 0;
  size_t actionable = 0, requires_confirmation = 0, logged_only = 0;
  std::map<ErrorCode, size_t> rejected;
  size_t totalRejected() const;
};

class Pipeline {
 public:
  Pipeline(PipelineConfig cfg, SessionPolicy policy, EventStore& store, SessionRouter& router,
           Clock clock = systemNow);
  Outcome submit(const std::string& raw);
  const Stats& stats() const { return stats_; }

 private:
  Outcome reject(const Error& e, const std::string& id = {});
  PipelineConfig cfg_;
  SessionPolicy policy_;
  EventStore& store_;
  SessionRouter& router_;
  Clock clock_;
  Stats stats_;
};

}  // namespace dreamlink
