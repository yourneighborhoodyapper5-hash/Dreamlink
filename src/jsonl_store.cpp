#include "jsonl_store.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

#include "codec.hpp"

namespace dreamlink {

const char* toString(Disposition d) {
  switch (d) {
    case Disposition::Actionable: return "actionable";
    case Disposition::RequiresConfirmation: return "requires_confirmation";
    case Disposition::LoggedOnly: return "logged_only";
  }
  return "unknown";
}

bool dispositionFromString(const std::string& s, Disposition& out) {
  if (s == "actionable") { out = Disposition::Actionable; return true; }
  if (s == "requires_confirmation") { out = Disposition::RequiresConfirmation; return true; }
  if (s == "logged_only") { out = Disposition::LoggedOnly; return true; }
  return false;
}

namespace {

std::string errnoText(const char* what) {
  return std::string(what) + ": " + std::strerror(errno);
}

std::string encodeRecord(const LogRecord& r) {
  return "{\"received_at\":" + json::quote(formatTimestamp(r.received_at)) +
         ",\"disposition\":" + json::quote(toString(r.disposition)) +
         ",\"event\":" + encodeEvent(r.event) + "}";
}

bool decodeRecord(const std::string& line, LogRecord& out, std::string& err) {
  json::Value root;
  if (!json::parse(line, root, err)) return false;
  if (root.type != json::Value::Type::Object) { err = "record is not an object"; return false; }
  const json::Value* ra = root.find("received_at");
  const json::Value* di = root.find("disposition");
  const json::Value* ev = root.find("event");
  if (!ra || ra->type != json::Value::Type::String || !di ||
      di->type != json::Value::Type::String || !ev) {
    err = "record is missing received_at, disposition, or event";
    return false;
  }
  if (!parseTimestamp(ra->string, out.received_at)) { err = "bad received_at"; return false; }
  if (!dispositionFromString(di->string, out.disposition)) { err = "bad disposition"; return false; }
  DecodeResult dr = decodeEventValue(*ev, Limits{});
  if (!dr.ok()) { err = std::string("bad event (") + toString(dr.error.code) + ")"; return false; }
  out.event = std::move(dr.event);
  return true;
}

bool readWholeFile(int fd, std::string& out, std::string& err) {
  out.clear();
  char buf[65536];
  off_t off = 0;
  for (;;) {
    ssize_t n = ::pread(fd, buf, sizeof buf, off);
    if (n < 0) {
      if (errno == EINTR) continue;
      err = errnoText("read log");
      return false;
    }
    if (n == 0) return true;
    out.append(buf, static_cast<size_t>(n));
    off += n;
  }
}

// Parses all complete lines. good_end = byte offset just past the last
// complete, valid line. A trailing fragment without '\n' is left unparsed.
bool parseLog(const std::string& content, std::vector<LogRecord>& records,
              size_t& good_end, std::string& err) {
  size_t pos = 0;
  size_t line_no = 0;
  good_end = 0;
  while (pos < content.size()) {
    size_t nl = content.find('\n', pos);
    if (nl == std::string::npos) break;  // torn tail
    ++line_no;
    LogRecord rec;
    std::string why;
    if (!decodeRecord(content.substr(pos, nl - pos), rec, why)) {
      err = "corrupt log record on line " + std::to_string(line_no) + ": " + why;
      return false;
    }
    records.push_back(std::move(rec));
    pos = nl + 1;
    good_end = pos;
  }
  return true;
}

}  // namespace

std::unique_ptr<JsonlEventStore> JsonlEventStore::open(const std::string& path, bool fsync_each,
                                                       std::string& error,
                                                       size_t* recovered_bytes) {
  if (recovered_bytes) *recovered_bytes = 0;
  try {
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  } catch (const std::filesystem::filesystem_error& e) {
    error = std::string("cannot create log directory: ") + e.what();
    return nullptr;
  }
  // 0600: the log may eventually hold sensitive physiological context.
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0) { error = errnoText("open log"); return nullptr; }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    error = "log is locked by another process";
    ::close(fd);
    return nullptr;
  }
  std::string content;
  if (!readWholeFile(fd, content, error)) { ::close(fd); return nullptr; }

  std::vector<LogRecord> records;
  size_t good_end = 0;
  if (!parseLog(content, records, good_end, error)) { ::close(fd); return nullptr; }

  if (good_end < content.size()) {
    // Torn final write: drop the partial record. It was never acknowledged
    // to the sender (acks happen only after fsync), so the sender retries.
    if (::ftruncate(fd, static_cast<off_t>(good_end)) != 0 || ::fsync(fd) != 0) {
      error = errnoText("truncate torn record");
      ::close(fd);
      return nullptr;
    }
    if (recovered_bytes) *recovered_bytes = content.size() - good_end;
  }

  std::unique_ptr<JsonlEventStore> store(new JsonlEventStore(fd, path, fsync_each));
  store->bytes_ = good_end;
  for (const auto& r : records) {
    if (!store->ids_.insert(r.event.id).second) {
      error = "log contains duplicate event id " + r.event.id;
      return nullptr;  // destructor closes fd
    }
  }
  return store;
}

JsonlEventStore::~JsonlEventStore() {
  if (fd_ >= 0) ::close(fd_);  // closing releases the flock
}

bool JsonlEventStore::contains(const std::string& event_id) const {
  return ids_.count(event_id) != 0;
}

AppendResult JsonlEventStore::append(const LogRecord& rec, std::string& error) {
  if (ids_.count(rec.event.id)) return AppendResult::Duplicate;
  std::string line = encodeRecord(rec);
  line.push_back('\n');

  auto rollback = [&]() {
    int rc = ::ftruncate(fd_, static_cast<off_t>(bytes_));
    (void)rc;  // best effort; the next open() also repairs a torn tail
  };

  size_t off = 0;
  while (off < line.size()) {
    ssize_t n = ::write(fd_, line.data() + off, line.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      error = errnoText("write log");
      rollback();
      return AppendResult::Failed;
    }
    off += static_cast<size_t>(n);
  }
  if (sync_ && ::fsync(fd_) != 0) {
    error = errnoText("fsync log");
    rollback();
    return AppendResult::Failed;
  }
  bytes_ += line.size();
  ids_.insert(rec.event.id);
  return AppendResult::Inserted;
}

std::vector<LogRecord> JsonlEventStore::readAll(std::string& error) const {
  std::vector<LogRecord> records;
  std::string content;
  if (!readWholeFile(fd_, content, error)) return {};
  size_t good_end = 0;
  if (!parseLog(content, records, good_end, error)) return {};
  return records;
}

}  // namespace dreamlink
