// Append-only JSON-lines event log with fsync, single-writer locking,
// duplicate detection, and recovery from a torn final write.
#pragma once
#include <unordered_set>

#include "store.hpp"

namespace dreamlink {

class JsonlEventStore : public EventStore {
 public:
  // Opens (creating if needed) the log. On a torn final record (crash during
  // write) the partial bytes are truncated and reported via recovered_bytes.
  // A corrupt record in the middle of the file is NOT silently skipped:
  // open fails so a human can look at it.
  static std::unique_ptr<JsonlEventStore> open(const std::string& path, bool fsync_each,
                                               std::string& error,
                                               size_t* recovered_bytes = nullptr);
  ~JsonlEventStore() override;

  AppendResult append(const LogRecord& rec, std::string& error) override;
  bool contains(const std::string& event_id) const override;
  size_t size() const override { return ids_.size(); }
  std::vector<LogRecord> readAll(std::string& error) const override;

 private:
  JsonlEventStore(int fd, std::string path, bool sync) : fd_(fd), path_(std::move(path)), sync_(sync) {}
  int fd_;
  std::string path_;
  bool sync_;
  uint64_t bytes_ = 0;  // known-good length of the file
  std::unordered_set<std::string> ids_;
};

}  // namespace dreamlink
