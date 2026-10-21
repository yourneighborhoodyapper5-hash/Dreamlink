// Append-only event log interface. Backends: JSONL file (no dependencies)
// and SQLite (optional, build with SQLITE=1).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "event.hpp"

namespace dreamlink {

// What the server decided to do with an accepted event.
enum class Disposition {
  Actionable,            // may be routed to participants
  RequiresConfirmation,  // routed only as a *request*; a human/manual action decides
  LoggedOnly             // recorded for research, never acted on (low confidence)
};

const char* toString(Disposition d);
bool dispositionFromString(const std::string& s, Disposition& out);

struct LogRecord {
  Event event;
  Disposition disposition = Disposition::LoggedOnly;
  TimePoint received_at{};
};

enum class AppendResult { Inserted, Duplicate, Failed };

class EventStore {
 public:
  virtual ~EventStore() = default;
  // Inserted only after the record is durable (fsync / committed transaction).
  // Duplicate = an event with this id is already stored; nothing is written.
  virtual AppendResult append(const LogRecord& rec, std::string& error) = 0;
  virtual bool contains(const std::string& event_id) const = 0;
  virtual size_t size() const = 0;
  virtual std::vector<LogRecord> readAll(std::string& error) const = 0;
};

}  // namespace dreamlink
