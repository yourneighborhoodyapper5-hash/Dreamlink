#ifdef DREAMLINK_WITH_SQLITE
#include "sqlite_store.hpp"

#include <sqlite3.h>

#include <filesystem>

#include "codec.hpp"
#include "json.hpp"

namespace dreamlink {

namespace {

struct Stmt {
  sqlite3_stmt* s = nullptr;
  ~Stmt() {
    if (s) sqlite3_finalize(s);
  }
};

bool execSql(sqlite3* db, const char* sql, std::string& err) {
  char* msg = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
    err = msg ? msg : "sqlite error";
    sqlite3_free(msg);
    return false;
  }
  return true;
}

// Append-only is enforced by the database itself, not just by our code.
const char* kSchema =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA synchronous=FULL;"
    "CREATE TABLE IF NOT EXISTS event_log ("
    "  seq INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  id TEXT NOT NULL UNIQUE,"
    "  session_id TEXT NOT NULL,"
    "  type TEXT NOT NULL,"
    "  confidence REAL NOT NULL CHECK (confidence >= 0 AND confidence <= 1),"
    "  timestamp_ms INTEGER NOT NULL,"
    "  source_id TEXT,"
    "  payload_json TEXT NOT NULL,"
    "  disposition TEXT NOT NULL,"
    "  received_at_ms INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS event_log_session ON event_log(session_id, seq);"
    "CREATE TRIGGER IF NOT EXISTS event_log_no_update BEFORE UPDATE ON event_log "
    "BEGIN SELECT RAISE(ABORT, 'event_log is append-only'); END;"
    "CREATE TRIGGER IF NOT EXISTS event_log_no_delete BEFORE DELETE ON event_log "
    "BEGIN SELECT RAISE(ABORT, 'event_log is append-only'); END;";

std::string payloadToJson(const Payload& p) {
  json::Value o = json::Value::makeObject();
  for (const auto& kv : p) o.set(kv.first, json::Value::makeString(kv.second));
  return json::serialize(o);
}

}  // namespace

std::unique_ptr<SqliteEventStore> SqliteEventStore::open(const std::string& path,
                                                         std::string& error) {
  try {
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  } catch (const std::filesystem::filesystem_error& e) {
    error = std::string("cannot create database directory: ") + e.what();
    return nullptr;
  }
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                           nullptr);
  if (rc != SQLITE_OK) {
    error = db ? sqlite3_errmsg(db) : "cannot open database";
    if (db) sqlite3_close(db);
    return nullptr;
  }
  sqlite3_extended_result_codes(db, 1);
  sqlite3_busy_timeout(db, 5000);
  if (!execSql(db, kSchema, error)) {
    sqlite3_close(db);
    return nullptr;
  }
  std::error_code ec;  // restrict to owner; ignore failure
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
  return std::unique_ptr<SqliteEventStore>(new SqliteEventStore(db));
}

SqliteEventStore::~SqliteEventStore() {
  if (db_) sqlite3_close(db_);
}

AppendResult SqliteEventStore::append(const LogRecord& rec, std::string& error) {
  Stmt st;
  const char* sql =
      "INSERT INTO event_log (id, session_id, type, confidence, timestamp_ms, source_id,"
      " payload_json, disposition, received_at_ms) VALUES (?,?,?,?,?,?,?,?,?)";
  if (sqlite3_prepare_v2(db_, sql, -1, &st.s, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(db_);
    return AppendResult::Failed;
  }
  const Event& e = rec.event;
  std::string type = toWire(e.type);
  std::string payload = payloadToJson(e.payload);
  std::string disp = toString(rec.disposition);
  sqlite3_bind_text(st.s, 1, e.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 2, e.session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 3, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st.s, 4, e.confidence);
  sqlite3_bind_int64(st.s, 5, toEpochMillis(e.timestamp));
  if (e.source_id) sqlite3_bind_text(st.s, 6, e.source_id->c_str(), -1, SQLITE_TRANSIENT);
  else sqlite3_bind_null(st.s, 6);
  sqlite3_bind_text(st.s, 7, payload.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 8, disp.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st.s, 9, toEpochMillis(rec.received_at));

  int rc = sqlite3_step(st.s);
  if (rc == SQLITE_DONE) return AppendResult::Inserted;
  int ext = sqlite3_extended_errcode(db_);
  if (ext == SQLITE_CONSTRAINT_UNIQUE || ext == SQLITE_CONSTRAINT_PRIMARYKEY)
    return AppendResult::Duplicate;
  error = sqlite3_errmsg(db_);
  return AppendResult::Failed;
}

bool SqliteEventStore::contains(const std::string& event_id) const {
  Stmt st;
  if (sqlite3_prepare_v2(db_, "SELECT 1 FROM event_log WHERE id = ?", -1, &st.s, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(st.s, 1, event_id.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(st.s) == SQLITE_ROW;
}

size_t SqliteEventStore::size() const {
  Stmt st;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM event_log", -1, &st.s, nullptr) != SQLITE_OK)
    return 0;
  if (sqlite3_step(st.s) != SQLITE_ROW) return 0;
  return static_cast<size_t>(sqlite3_column_int64(st.s, 0));
}

std::vector<LogRecord> SqliteEventStore::readAll(std::string& error) const {
  std::vector<LogRecord> out;
  Stmt st;
  const char* sql =
      "SELECT id, session_id, type, confidence, timestamp_ms, source_id, payload_json,"
      " disposition, received_at_ms FROM event_log ORDER BY seq";
  if (sqlite3_prepare_v2(db_, sql, -1, &st.s, nullptr) != SQLITE_OK) {
    error = sqlite3_errmsg(db_);
    return {};
  }
  auto text = [&](int col) {
    const unsigned char* t = sqlite3_column_text(st.s, col);
    return std::string(t ? reinterpret_cast<const char*>(t) : "");
  };
  int rc;
  while ((rc = sqlite3_step(st.s)) == SQLITE_ROW) {
    LogRecord r;
    r.event.id = text(0);
    r.event.session_id = text(1);
    if (!eventTypeFromWire(text(2), r.event.type)) { error = "bad type in database"; return {}; }
    r.event.confidence = sqlite3_column_double(st.s, 3);
    r.event.timestamp = fromEpochMillis(sqlite3_column_int64(st.s, 4));
    if (sqlite3_column_type(st.s, 5) != SQLITE_NULL) r.event.source_id = text(5);
    json::Value pv;
    std::string perr;
    if (!json::parse(text(6), pv, perr) || pv.type != json::Value::Type::Object) {
      error = "bad payload_json in database";
      return {};
    }
    for (const auto& kv : pv.object) r.event.payload[kv.first] = kv.second.string;
    if (!dispositionFromString(text(7), r.disposition)) { error = "bad disposition in database"; return {}; }
    r.received_at = fromEpochMillis(sqlite3_column_int64(st.s, 8));
    out.push_back(std::move(r));
  }
  if (rc != SQLITE_DONE) { error = sqlite3_errmsg(db_); return {}; }
  return out;
}

}  // namespace dreamlink
#endif  // DREAMLINK_WITH_SQLITE
