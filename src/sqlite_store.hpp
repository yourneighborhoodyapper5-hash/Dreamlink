// Optional SQLite backend. Compiled only with `make SQLITE=1`
// (needs libsqlite3-dev: sudo apt install libsqlite3-dev).
#pragma once
#ifdef DREAMLINK_WITH_SQLITE
#include "store.hpp"

struct sqlite3;

namespace dreamlink {

class SqliteEventStore : public EventStore {
 public:
  static std::unique_ptr<SqliteEventStore> open(const std::string& path, std::string& error);
  ~SqliteEventStore() override;

  AppendResult append(const LogRecord& rec, std::string& error) override;
  bool contains(const std::string& event_id) const override;
  size_t size() const override;
  std::vector<LogRecord> readAll(std::string& error) const override;

 private:
  explicit SqliteEventStore(sqlite3* db) : db_(db) {}
  sqlite3* db_;
};

}  // namespace dreamlink
#endif
