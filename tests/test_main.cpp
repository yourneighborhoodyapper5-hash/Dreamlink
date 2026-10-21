// Dependency-free test runner. Each TEST is run; failures are reported with
// file/line and the process exits non-zero if anything failed.
#include <fcntl.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "codec.hpp"
#include "event.hpp"
#include "json.hpp"
#include "jsonl_store.hpp"
#include "pipeline.hpp"
#include "simulator.hpp"

using namespace dreamlink;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- framework
static int g_checks = 0, g_failures = 0;
static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}
struct Registrar {
  Registrar(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                          \
  static void name();                       \
  static Registrar reg_##name(#name, name); \
  static void name()
#define CHECK(cond)                                                              \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!(cond)) {                                                               \
      ++g_failures;                                                              \
      std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
    }                                                                            \
  } while (0)
#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    ++g_checks;                                                                            \
    auto va = (a);                                                                         \
    auto vb = (b);                                                                         \
    if (!(va == vb)) {                                                                     \
      ++g_failures;                                                                        \
      std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #a " == " #b "\n";     \
    }                                                                                      \
  } while (0)

// ------------------------------------------------------------------ helpers
static fs::path tmpRoot() {
  const char* e = std::getenv("DREAMLINK_TEST_TMP");
  return fs::path(e ? e : "build/test-tmp");
}

// A fresh directory owned by one test; removed afterwards. Only ever deletes
// the directory this test created.
struct TempDir {
  fs::path path;
  explicit TempDir(const std::string& name) {
    path = tmpRoot() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  std::string file(const std::string& n) const { return (path / n).string(); }
};

static const TimePoint kNow = fromEpochMillis(1792000000000LL);  // fixed test clock
static Clock fixedClock() { return [] { return kNow; }; }

static Event sampleEvent(UuidGenerator& gen) {
  Event e;
  e.id = gen.next();
  e.session_id = "dream-alpha";
  e.type = EventType::PlayerAction;
  e.confidence = 0.9;
  e.timestamp = kNow - std::chrono::seconds(1);
  e.payload = {{"action", "move"}, {"direction", "north"}};
  e.source_id = "sim-device-01";
  return e;
}

static json::Value parsed(const std::string& s) {
  json::Value v;
  std::string err;
  bool ok = json::parse(s, v, err);
  if (!ok) std::cerr << "test helper: bad json: " << err << "\n";
  return v;
}

static ErrorCode decodeCode(const std::string& raw) {
  return decodeEvent(raw, Limits{}).error.code;
}

// A valid wire message to mutate.
static std::string validRaw() {
  UuidGenerator gen(7);
  return encodeEvent(sampleEvent(gen));
}

static SessionPolicy defaultPolicy() {
  SessionPolicy p;
  p.registerSession("dream-alpha", {"sim-device-01", "sim-device-02"});
  p.registerSession("dream-beta", {});  // any source
  return p;
}

// -------------------------------------------------------------------- UUIDs
TEST(uuid_format_and_uniqueness) {
  UuidGenerator gen;
  std::set<std::string> seen;
  for (int i = 0; i < 2000; ++i) {
    std::string id = gen.next();
    CHECK(isUuidV4(id));
    seen.insert(id);
  }
  CHECK_EQ(seen.size(), size_t(2000));
}

TEST(uuid_seeded_is_deterministic) {
  UuidGenerator a(42), b(42), c(43);
  CHECK_EQ(a.next(), b.next());
  CHECK(a.next() != c.next());
}

TEST(uuid_validator_rejects_bad_forms) {
  CHECK(!isUuidV4(""));
  CHECK(!isUuidV4("12345"));
  CHECK(!isUuidV4("550E8400-E29B-41D4-A716-446655440000"));  // uppercase
  CHECK(!isUuidV4("550e8400-e29b-11d4-a716-446655440000"));  // version 1
  CHECK(!isUuidV4("550e8400-e29b-41d4-c716-446655440000"));  // bad variant
  CHECK(!isUuidV4("550e8400e29b41d4a716446655440000"));      // no dashes
  CHECK(isUuidV4("550e8400-e29b-41d4-a716-446655440000"));
}

// --------------------------------------------------------------- timestamps
TEST(timestamp_format_and_roundtrip) {
  CHECK_EQ(formatTimestamp(fromEpochMillis(0)), std::string("1970-01-01T00:00:00.000Z"));
  CHECK_EQ(formatTimestamp(fromEpochMillis(1)), std::string("1970-01-01T00:00:00.001Z"));
  CHECK_EQ(formatTimestamp(fromEpochMillis(-1)), std::string("1969-12-31T23:59:59.999Z"));
  TimePoint tp;
  CHECK(parseTimestamp("2026-10-21T12:34:56.789Z", tp));
  CHECK_EQ(formatTimestamp(tp), std::string("2026-10-21T12:34:56.789Z"));
  CHECK(parseTimestamp("2028-02-29T00:00:00.000Z", tp));  // leap day
  for (int64_t ms : {int64_t(0), int64_t(951782400000LL), int64_t(1792000000123LL), int64_t(4102444800999LL)}) {
    TimePoint back;
    CHECK(parseTimestamp(formatTimestamp(fromEpochMillis(ms)), back));
    CHECK_EQ(toEpochMillis(back), ms);
  }
}

TEST(timestamp_rejects_malformed) {
  TimePoint tp;
  CHECK(!parseTimestamp("2027-02-29T00:00:00.000Z", tp));  // not a leap year
  CHECK(!parseTimestamp("2026-02-30T00:00:00.000Z", tp));
  CHECK(!parseTimestamp("2026-13-01T00:00:00.000Z", tp));
  CHECK(!parseTimestamp("2026-10-21T24:00:00.000Z", tp));
  CHECK(!parseTimestamp("2026-10-21T12:60:00.000Z", tp));
  CHECK(!parseTimestamp("2026-10-21t12:00:00.000Z", tp));   // lowercase t
  CHECK(!parseTimestamp("2026-10-21T12:00:00.000", tp));    // no Z
  CHECK(!parseTimestamp("2026-10-21T12:00:00Z", tp));       // no millis
  CHECK(!parseTimestamp("2026-10-21T12:00:00.000+00:00", tp));
  CHECK(!parseTimestamp("", tp));
  CHECK(!parseTimestamp("yesterday", tp));
}

// --------------------------------------------------------------------- JSON
TEST(json_parses_valid_documents) {
  json::Value v = parsed(R"({"a":1,"b":[true,false,null],"c":"x\n\u00e9\ud83d\ude00","d":-1.5e2})");
  CHECK(v.type == json::Value::Type::Object);
  CHECK_EQ(v.find("a")->number, 1.0);
  CHECK_EQ(v.find("b")->array.size(), size_t(3));
  CHECK_EQ(v.find("c")->string, std::string("x\n\xc3\xa9\xf0\x9f\x98\x80"));
  CHECK_EQ(v.find("d")->number, -150.0);
}

TEST(json_rejects_invalid_documents) {
  const char* bad[] = {
      "",                       "   ",                  "{",
      "{\"a\":1,}",             "[1,]",                 "{\"a\" 1}",
      "{'a':1}",                "{\"a\":1} x",          "{\"a\":1}{\"b\":2}",
      "{\"a\":1,\"a\":2}",      "01",                   "1.",
      ".5",                     "+1",                   "NaN",
      "Infinity",               "{\"a\":NaN}",          "1e999",
      "\"abc",                  "\"a\tb\"",             "\"\\x\"",
      "\"\\ud800\"",            "\"\\udc00\"",          "\"\\ud800\\u0041\"",
      "\"\\u0000\"",            "[[[[[[[[[[1]]]]]]]]]]",  // deeper than 8
      "tru",                    "nul",
  };
  for (const char* s : bad) {
    json::Value v;
    std::string err;
    ++g_checks;
    if (json::parse(s, v, err)) {
      ++g_failures;
      std::cerr << "  FAIL json accepted invalid input: " << s << "\n";
    }
  }
  json::Value v;
  std::string err;
  CHECK(!json::parse(std::string("\"\xff\xfe\""), v, err));          // invalid UTF-8
  CHECK(!json::parse(std::string("\"\xc0\xaf\""), v, err));          // overlong
  CHECK(!json::parse(std::string("\"\xed\xa0\x80\""), v, err));      // encoded surrogate
}

TEST(json_serialize_roundtrip) {
  std::string src = R"({"a":[1,2.5,"q\"uote",null,true],"b":{"c":"\u0001"}})";
  json::Value v = parsed(src);
  json::Value v2 = parsed(json::serialize(v));
  CHECK_EQ(json::serialize(v), json::serialize(v2));
  CHECK_EQ(json::numberToString(0.85), std::string("0.85"));
  CHECK_EQ(json::numberToString(1.0), std::string("1"));
  CHECK_EQ(json::numberToString(std::nan("")), std::string("null"));
}

// -------------------------------------------------------- encode / decode
TEST(event_encode_decode_roundtrip) {
  UuidGenerator gen(1);
  Event e = sampleEvent(gen);
  e.payload["text"] = "quote \" backslash \\ newline \n tab \t unicode \xc3\xa9 \xf0\x9f\x98\x80 ctrl \x01";
  std::string wire = encodeEvent(e);
  CHECK(wire.find('\n') == std::string::npos);  // one line: safe for the JSONL log
  DecodeResult r = decodeEvent(wire, Limits{});
  CHECK(r.ok());
  CHECK_EQ(r.event.id, e.id);
  CHECK_EQ(r.event.session_id, e.session_id);
  CHECK(r.event.type == e.type);
  CHECK_EQ(r.event.confidence, e.confidence);
  CHECK_EQ(toEpochMillis(r.event.timestamp), toEpochMillis(e.timestamp));
  CHECK(r.event.payload == e.payload);
  CHECK(r.event.source_id == e.source_id);
  CHECK_EQ(encodeEvent(r.event), wire);  // canonical form is stable
}

TEST(event_without_optional_source_roundtrips) {
  UuidGenerator gen(2);
  Event e = sampleEvent(gen);
  e.source_id.reset();
  e.payload.clear();
  DecodeResult r = decodeEvent(encodeEvent(e), Limits{});
  CHECK(r.ok());
  CHECK(!r.event.source_id.has_value());
  CHECK(r.event.payload.empty());
}

TEST(all_event_types_roundtrip) {
  UuidGenerator gen(3);
  for (EventType t : {EventType::PlayerAction, EventType::Message, EventType::LogoffIntent}) {
    Event e = sampleEvent(gen);
    e.type = t;
    DecodeResult r = decodeEvent(encodeEvent(e), Limits{});
    CHECK(r.ok());
    CHECK(r.event.type == t);
  }
}

// --------------------------------------------------------------- validation
TEST(decode_rejects_each_malformed_shape) {
  const std::string good = validRaw();
  auto mutate = [&](const std::function<void(json::Value&)>& f) {
    json::Value v = parsed(good);
    f(v);
    return json::serialize(v);
  };
  using json::Value;
  CHECK(decodeEvent(good, Limits{}).ok());

  CHECK(decodeCode("") == ErrorCode::MalformedJson);
  CHECK(decodeCode("not json") == ErrorCode::MalformedJson);
  CHECK(decodeCode(good.substr(0, good.size() / 2)) == ErrorCode::MalformedJson);
  CHECK(decodeCode("[1,2,3]") == ErrorCode::SchemaViolation);
  CHECK(decodeCode("\"hello\"") == ErrorCode::SchemaViolation);

  for (const char* field : {"v", "id", "session_id", "type", "confidence", "timestamp", "payload"})
    CHECK(decodeCode(mutate([&](Value& v) { v.remove(field); })) == ErrorCode::SchemaViolation);

  CHECK(decodeCode(mutate([](Value& v) { v.set("extra", Value::makeNumber(1)); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("v", Value::makeString("1")); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("v", Value::makeNumber(2)); })) == ErrorCode::UnsupportedVersion);
  CHECK(decodeCode(mutate([](Value& v) { v.set("v", Value::makeNumber(1.5)); })) == ErrorCode::UnsupportedVersion);
  CHECK(decodeCode(mutate([](Value& v) { v.set("id", Value::makeNumber(5)); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("id", Value::makeString("abc")); })) == ErrorCode::InvalidId);
  CHECK(decodeCode(mutate([](Value& v) { v.set("type", Value::makeString("message")); })) == ErrorCode::UnknownEventType);
  CHECK(decodeCode(mutate([](Value& v) { v.set("type", Value::makeNull()); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("timestamp", Value::makeString("now")); })) == ErrorCode::InvalidTimestamp);
  CHECK(decodeCode(mutate([](Value& v) { v.set("timestamp", Value::makeNumber(1)); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("payload", Value::makeArray()); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) {
          Value p = Value::makeObject();
          p.set("n", Value::makeNumber(1));
          v.set("payload", p);
        })) == ErrorCode::SchemaViolation);  // payload values must be strings
  CHECK(decodeCode(mutate([](Value& v) { v.set("source_id", Value::makeNumber(1)); })) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(mutate([](Value& v) { v.set("source_id", Value::makeString("bad id!")); })) == ErrorCode::InvalidSourceId);
  CHECK(decodeCode(mutate([](Value& v) { v.set("session_id", Value::makeString("")); })) == ErrorCode::InvalidSessionId);
  CHECK(decodeCode(mutate([](Value& v) { v.set("session_id", Value::makeString("has space")); })) == ErrorCode::InvalidSessionId);
  CHECK(decodeCode(mutate([](Value& v) { v.set("session_id", Value::makeString(std::string(65, 'a'))); })) == ErrorCode::InvalidSessionId);
}

TEST(confidence_boundaries) {
  const std::string good = validRaw();
  auto withConf = [&](const std::string& literal) {
    json::Value v = parsed(good);
    std::string s = json::serialize(v);
    size_t p = s.find("\"confidence\":");
    size_t q = s.find(',', p);
    return s.substr(0, p) + "\"confidence\":" + literal + s.substr(q);
  };
  CHECK(decodeEvent(withConf("0"), Limits{}).ok());
  CHECK(decodeEvent(withConf("1"), Limits{}).ok());
  CHECK(decodeEvent(withConf("0.5"), Limits{}).ok());
  CHECK(decodeCode(withConf("1.0000001")) == ErrorCode::InvalidConfidence);
  CHECK(decodeCode(withConf("-0.0000001")) == ErrorCode::InvalidConfidence);
  CHECK(decodeCode(withConf("2")) == ErrorCode::InvalidConfidence);
  CHECK(decodeCode(withConf("-1")) == ErrorCode::InvalidConfidence);
  CHECK(decodeCode(withConf("1e308")) == ErrorCode::InvalidConfidence);
  CHECK(decodeCode(withConf("NaN")) == ErrorCode::MalformedJson);
  CHECK(decodeCode(withConf("Infinity")) == ErrorCode::MalformedJson);
  CHECK(decodeCode(withConf("1e999")) == ErrorCode::MalformedJson);
  CHECK(decodeCode(withConf("null")) == ErrorCode::SchemaViolation);
  CHECK(decodeCode(withConf("\"0.5\"")) == ErrorCode::SchemaViolation);
  // validateEvent also guards events built in code (not just parsed ones).
  UuidGenerator gen(5);
  Event e = sampleEvent(gen);
  e.confidence = std::nan("");
  CHECK(validateEvent(e, Limits{}).code == ErrorCode::InvalidConfidence);
  e.confidence = INFINITY;
  CHECK(validateEvent(e, Limits{}).code == ErrorCode::InvalidConfidence);
}

TEST(size_and_payload_limits) {
  UuidGenerator gen(6);
  Limits lim;
  Event e = sampleEvent(gen);

  e.payload.clear();
  for (size_t i = 0; i < lim.max_payload_entries; ++i) e.payload["k" + std::to_string(i)] = "v";
  CHECK(decodeEvent(encodeEvent(e), lim).ok());  // exactly at the limit
  e.payload["one_too_many"] = "v";
  CHECK(decodeEvent(encodeEvent(e), lim).error.code == ErrorCode::PayloadTooLarge);

  e.payload = {{"text", std::string(lim.max_payload_value_bytes, 'x')}};
  CHECK(decodeEvent(encodeEvent(e), lim).ok());
  e.payload = {{"text", std::string(lim.max_payload_value_bytes + 1, 'x')}};
  CHECK(decodeEvent(encodeEvent(e), lim).error.code == ErrorCode::PayloadTooLarge);

  e.payload = {{"bad key", "v"}};
  CHECK(decodeEvent(encodeEvent(e), lim).error.code == ErrorCode::InvalidPayload);
  e.payload = {{"", "v"}};
  CHECK(decodeEvent(encodeEvent(e), lim).error.code == ErrorCode::InvalidPayload);
  e.payload = {{std::string(65, 'k'), "v"}};
  CHECK(decodeEvent(encodeEvent(e), lim).error.code == ErrorCode::InvalidPayload);

  std::string huge(lim.max_message_bytes + 1, ' ');
  CHECK(decodeEvent(huge, lim).error.code == ErrorCode::MessageTooLarge);
  CHECK(decodeEvent(validRaw() + std::string(lim.max_message_bytes, ' '), lim).error.code ==
        ErrorCode::MessageTooLarge);
}

TEST(timestamp_window) {
  UuidGenerator gen(8);
  Limits lim;
  Event e = sampleEvent(gen);
  e.timestamp = kNow;
  CHECK(checkTimestampWindow(e, kNow, lim).ok());
  e.timestamp = kNow + lim.max_future_skew;
  CHECK(checkTimestampWindow(e, kNow, lim).ok());
  e.timestamp = kNow + lim.max_future_skew + std::chrono::milliseconds(1);
  CHECK(checkTimestampWindow(e, kNow, lim).code == ErrorCode::TimestampOutOfWindow);
  e.timestamp = kNow - lim.max_age;
  CHECK(checkTimestampWindow(e, kNow, lim).ok());
  e.timestamp = kNow - lim.max_age - std::chrono::milliseconds(1);
  CHECK(checkTimestampWindow(e, kNow, lim).code == ErrorCode::TimestampOutOfWindow);
}

TEST(error_messages_do_not_leak_payload_content) {
  UuidGenerator gen(9);
  Event e = sampleEvent(gen);
  e.payload = {{"text", "SECRET-DREAM-CONTENT"}};
  e.confidence = 5.0;  // invalid
  std::string wire = encodeEvent(e);
  DecodeResult r = decodeEvent(wire, Limits{});
  CHECK(!r.ok());
  CHECK(r.error.detail.find("SECRET") == std::string::npos);
}

// ----------------------------------------------------------- classification
TEST(classification_policy) {
  UuidGenerator gen(10);
  Event e = sampleEvent(gen);
  e.type = EventType::PlayerAction;
  e.confidence = 0.79;
  CHECK(classify(e, 0.8) == Disposition::LoggedOnly);
  e.confidence = 0.80;
  CHECK(classify(e, 0.8) == Disposition::Actionable);
  e.type = EventType::Message;
  e.confidence = 0.1;
  CHECK(classify(e, 0.8) == Disposition::LoggedOnly);
  e.confidence = 1.0;
  CHECK(classify(e, 0.8) == Disposition::Actionable);
  // LOGOFF_INTENT is never actionable on its own, even at confidence 1.0.
  e.type = EventType::LogoffIntent;
  e.confidence = 1.0;
  CHECK(classify(e, 0.8) == Disposition::RequiresConfirmation);
  e.confidence = 0.5;
  CHECK(classify(e, 0.8) == Disposition::LoggedOnly);
}

// ------------------------------------------------------------- JSONL store
TEST(jsonl_store_persists_across_reopen_and_dedups) {
  TempDir dir("store_persist");
  UuidGenerator gen(11);
  std::string err;
  std::vector<Event> evs;
  {
    auto s = JsonlEventStore::open(dir.file("log.jsonl"), true, err);
    CHECK(s != nullptr);
    for (int i = 0; i < 5; ++i) {
      evs.push_back(sampleEvent(gen));
      LogRecord r{evs.back(), Disposition::Actionable, kNow};
      CHECK(s->append(r, err) == AppendResult::Inserted);
    }
    CHECK_EQ(s->size(), size_t(5));
    LogRecord dup{evs[2], Disposition::Actionable, kNow};
    CHECK(s->append(dup, err) == AppendResult::Duplicate);
    CHECK_EQ(s->size(), size_t(5));
    CHECK(s->contains(evs[0].id));
    CHECK(!s->contains("nope"));
  }
  {
    auto s = JsonlEventStore::open(dir.file("log.jsonl"), true, err);
    CHECK(s != nullptr);
    CHECK_EQ(s->size(), size_t(5));
    LogRecord dup{evs[4], Disposition::LoggedOnly, kNow};
    CHECK(s->append(dup, err) == AppendResult::Duplicate);  // dedup survives restart
    auto all = s->readAll(err);
    CHECK_EQ(all.size(), size_t(5));
    for (size_t i = 0; i < all.size(); ++i) {
      CHECK_EQ(all[i].event.id, evs[i].id);
      CHECK(all[i].event.payload == evs[i].payload);
      CHECK(all[i].disposition == Disposition::Actionable);
    }
  }
}

TEST(jsonl_store_recovers_from_torn_final_write) {
  TempDir dir("store_torn");
  UuidGenerator gen(12);
  std::string err, path = dir.file("log.jsonl");
  Event first = sampleEvent(gen), second = sampleEvent(gen), lost = sampleEvent(gen);
  {
    auto s = JsonlEventStore::open(path, true, err);
    CHECK(s->append({first, Disposition::Actionable, kNow}, err) == AppendResult::Inserted);
    CHECK(s->append({second, Disposition::LoggedOnly, kNow}, err) == AppendResult::Inserted);
  }
  // Simulate a crash mid-write: half a record, no trailing newline.
  {
    std::string fragment = encodeEvent(lost);
    std::ofstream f(path, std::ios::app | std::ios::binary);
    f << "{\"received_at\":\"2026-10-21T00:00:00.000Z\",\"disposition\":\"actionable\",\"event\":"
      << fragment.substr(0, fragment.size() / 2);
  }
  size_t recovered = 0;
  auto s = JsonlEventStore::open(path, true, err, &recovered);
  CHECK(s != nullptr);
  CHECK(recovered > 0);
  CHECK_EQ(s->size(), size_t(2));
  CHECK(!s->contains(lost.id));
  // The sender never got an ack for the torn event, so it retries: must work.
  CHECK(s->append({lost, Disposition::Actionable, kNow}, err) == AppendResult::Inserted);
  s.reset();
  auto s2 = JsonlEventStore::open(path, true, err, &recovered);
  CHECK(s2 != nullptr);
  CHECK_EQ(recovered, size_t(0));
  CHECK_EQ(s2->size(), size_t(3));
  CHECK_EQ(s2->readAll(err).size(), size_t(3));
}

TEST(jsonl_store_refuses_corrupt_middle_record) {
  TempDir dir("store_corrupt");
  UuidGenerator gen(13);
  std::string err, path = dir.file("log.jsonl");
  {
    auto s = JsonlEventStore::open(path, true, err);
    CHECK(s->append({sampleEvent(gen), Disposition::Actionable, kNow}, err) == AppendResult::Inserted);
  }
  {
    std::ofstream f(path, std::ios::app | std::ios::binary);
    f << "this is not json\n";
  }
  {
    auto s = JsonlEventStore::open(path, true, err);
    CHECK(s == nullptr);
    CHECK(err.find("line 2") != std::string::npos);
  }
}

TEST(jsonl_store_detects_duplicate_ids_in_log) {
  TempDir dir("store_dupfile");
  UuidGenerator gen(14);
  std::string err, path = dir.file("log.jsonl");
  Event e = sampleEvent(gen);
  {
    auto s = JsonlEventStore::open(path, true, err);
    CHECK(s->append({e, Disposition::Actionable, kNow}, err) == AppendResult::Inserted);
  }
  {
    // Append the same line again behind the store's back.
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    std::ofstream out(path, std::ios::app);
    out << line << "\n";
  }
  CHECK(JsonlEventStore::open(path, true, err) == nullptr);
}

TEST(jsonl_store_single_writer_lock) {
  TempDir dir("store_lock");
  std::string err, path = dir.file("log.jsonl");
  auto a = JsonlEventStore::open(path, true, err);
  CHECK(a != nullptr);
  auto b = JsonlEventStore::open(path, true, err);
  CHECK(b == nullptr);
  CHECK(err.find("locked") != std::string::npos);
  a.reset();
  CHECK(JsonlEventStore::open(path, true, err) != nullptr);
}

TEST(jsonl_store_file_is_owner_only) {
  TempDir dir("store_perms");
  std::string err, path = dir.file("log.jsonl");
  auto s = JsonlEventStore::open(path, true, err);
  CHECK(s != nullptr);
  auto perms = fs::status(path).permissions();
  CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
}

// ----------------------------------------------------------------- pipeline
struct Harness {
  TempDir dir;
  std::unique_ptr<JsonlEventStore> store;
  SessionRouter router;
  std::vector<std::pair<Event, Disposition>> delivered;
  std::unique_ptr<Pipeline> pipe;
  UuidGenerator gen{99};

  explicit Harness(const std::string& name, double threshold = 0.8) : dir(name) {
    std::string err;
    store = JsonlEventStore::open(dir.file("log.jsonl"), true, err);
    for (const char* s : {"dream-alpha", "dream-beta"})
      router.subscribe(s, [this](const Event& e, Disposition d) { delivered.emplace_back(e, d); });
    PipelineConfig pc;
    pc.action_threshold = threshold;
    pipe = std::make_unique<Pipeline>(pc, defaultPolicy(), *store, router, fixedClock());
  }
  Event make(EventType t, double conf, const std::string& session = "dream-alpha",
             const std::string& source = "sim-device-01") {
    Event e = sampleEvent(gen);
    e.type = t;
    e.confidence = conf;
    e.session_id = session;
    e.source_id = source;
    return e;
  }
};

TEST(pipeline_accepts_logs_and_routes_actionable_event) {
  Harness h("pipe_basic");
  Event e = h.make(EventType::PlayerAction, 0.95);
  Outcome o = h.pipe->submit(encodeEvent(e));
  CHECK(o.status == Outcome::Status::Accepted);
  CHECK(o.disposition == Disposition::Actionable);
  CHECK_EQ(h.store->size(), size_t(1));
  CHECK_EQ(h.delivered.size(), size_t(1));
  CHECK_EQ(h.delivered[0].first.id, e.id);
}

TEST(pipeline_low_confidence_is_logged_but_never_routed) {
  Harness h("pipe_lowconf");
  Outcome o = h.pipe->submit(encodeEvent(h.make(EventType::PlayerAction, 0.30)));
  CHECK(o.status == Outcome::Status::Accepted);
  CHECK(o.disposition == Disposition::LoggedOnly);
  CHECK_EQ(h.store->size(), size_t(1));
  CHECK_EQ(h.delivered.size(), size_t(0));
  Outcome m = h.pipe->submit(encodeEvent(h.make(EventType::Message, 0.79)));
  CHECK(m.disposition == Disposition::LoggedOnly);
  CHECK_EQ(h.delivered.size(), size_t(0));
}

TEST(pipeline_logoff_intent_only_requests_confirmation) {
  Harness h("pipe_logoff");
  Outcome o = h.pipe->submit(encodeEvent(h.make(EventType::LogoffIntent, 1.0)));
  CHECK(o.status == Outcome::Status::Accepted);
  CHECK(o.disposition == Disposition::RequiresConfirmation);
  CHECK_EQ(h.delivered.size(), size_t(1));
  CHECK(h.delivered[0].second == Disposition::RequiresConfirmation);
  Outcome low = h.pipe->submit(encodeEvent(h.make(EventType::LogoffIntent, 0.4)));
  CHECK(low.disposition == Disposition::LoggedOnly);
  CHECK_EQ(h.delivered.size(), size_t(1));
}

TEST(pipeline_duplicate_is_acknowledged_but_not_reprocessed) {
  Harness h("pipe_dup");
  std::string raw = encodeEvent(h.make(EventType::PlayerAction, 0.9));
  CHECK(h.pipe->submit(raw).status == Outcome::Status::Accepted);
  CHECK(h.pipe->submit(raw).status == Outcome::Status::Duplicate);
  CHECK(h.pipe->submit(raw).status == Outcome::Status::Duplicate);
  CHECK_EQ(h.store->size(), size_t(1));
  CHECK_EQ(h.delivered.size(), size_t(1));
  CHECK_EQ(h.pipe->stats().duplicates, size_t(2));
}

TEST(pipeline_duplicate_after_restart_is_not_reprocessed) {
  TempDir dir("pipe_restart");
  UuidGenerator gen(21);
  std::string err, path = dir.file("log.jsonl"), raw;
  size_t delivered = 0;
  auto run = [&](const std::string& message) {
    auto store = JsonlEventStore::open(path, true, err);
    SessionRouter router;
    router.subscribe("dream-alpha", [&](const Event&, Disposition) { ++delivered; });
    Pipeline p(PipelineConfig{}, defaultPolicy(), *store, router, fixedClock());
    return p.submit(message).status;
  };
  Event e = sampleEvent(gen);
  raw = encodeEvent(e);
  CHECK(run(raw) == Outcome::Status::Accepted);
  CHECK(run(raw) == Outcome::Status::Duplicate);  // new process, same log
  CHECK_EQ(delivered, size_t(1));
}

TEST(pipeline_enforces_session_permissions) {
  Harness h("pipe_authz");
  Outcome unknown = h.pipe->submit(encodeEvent(h.make(EventType::Message, 0.9, "nope")));
  CHECK(unknown.status == Outcome::Status::Rejected);
  CHECK(unknown.error.code == ErrorCode::SessionForbidden);
  Outcome wrongDevice = h.pipe->submit(encodeEvent(h.make(EventType::Message, 0.9, "dream-alpha", "rogue")));
  CHECK(wrongDevice.error.code == ErrorCode::SessionForbidden);
  Event noSource = h.make(EventType::Message, 0.9);
  noSource.source_id.reset();
  CHECK(h.pipe->submit(encodeEvent(noSource)).error.code == ErrorCode::SessionForbidden);
  // A session registered with no source restriction accepts any source.
  CHECK(h.pipe->submit(encodeEvent(h.make(EventType::Message, 0.9, "dream-beta", "anything"))).status ==
        Outcome::Status::Accepted);
  CHECK_EQ(h.store->size(), size_t(1));  // rejected events are never persisted
}

TEST(pipeline_rejected_events_are_not_logged_or_routed) {
  Harness h("pipe_reject");
  Event old = h.make(EventType::Message, 0.9);
  old.timestamp = kNow - std::chrono::hours(48);
  CHECK(h.pipe->submit(encodeEvent(old)).error.code == ErrorCode::TimestampOutOfWindow);
  Event future = h.make(EventType::Message, 0.9);
  future.timestamp = kNow + std::chrono::minutes(10);
  CHECK(h.pipe->submit(encodeEvent(future)).error.code == ErrorCode::TimestampOutOfWindow);
  CHECK(h.pipe->submit("garbage").error.code == ErrorCode::MalformedJson);
  CHECK_EQ(h.store->size(), size_t(0));
  CHECK_EQ(h.delivered.size(), size_t(0));
  CHECK_EQ(h.pipe->stats().totalRejected(), size_t(3));
}

TEST(pipeline_survives_throwing_subscriber) {
  Harness h("pipe_throw");
  h.router.subscribe("dream-alpha", [](const Event&, Disposition) { throw std::runtime_error("boom"); });
  Outcome o = h.pipe->submit(encodeEvent(h.make(EventType::PlayerAction, 0.9)));
  CHECK(o.status == Outcome::Status::Accepted);
  CHECK_EQ(h.store->size(), size_t(1));
}

// A store that always fails, to prove the pipeline reports (and does not route).
class FailingStore : public EventStore {
 public:
  AppendResult append(const LogRecord&, std::string& error) override { error = "disk full"; return AppendResult::Failed; }
  bool contains(const std::string&) const override { return false; }
  size_t size() const override { return 0; }
  std::vector<LogRecord> readAll(std::string&) const override { return {}; }
};

TEST(pipeline_storage_failure_is_reported_and_not_routed) {
  FailingStore store;
  SessionRouter router;
  size_t delivered = 0;
  router.subscribe("dream-alpha", [&](const Event&, Disposition) { ++delivered; });
  Pipeline p(PipelineConfig{}, defaultPolicy(), store, router, fixedClock());
  UuidGenerator gen(31);
  Outcome o = p.submit(encodeEvent(sampleEvent(gen)));
  CHECK(o.status == Outcome::Status::StorageError);
  CHECK(o.error.code == ErrorCode::StorageFailure);
  CHECK_EQ(delivered, size_t(0));
  CHECK_EQ(p.stats().accepted, size_t(0));
}

// ---------------------------------------------------------------- simulator
static ErrorCode expectedError(Fault f) {
  switch (f) {
    case Fault::BadJson: return ErrorCode::MalformedJson;
    case Fault::MissingConfidence: return ErrorCode::SchemaViolation;
    case Fault::ConfidenceTooHigh: return ErrorCode::InvalidConfidence;
    case Fault::ConfidenceNegative: return ErrorCode::InvalidConfidence;
    case Fault::ConfidenceWrongType: return ErrorCode::SchemaViolation;
    case Fault::UnknownType: return ErrorCode::UnknownEventType;
    case Fault::OversizedPayload: return ErrorCode::PayloadTooLarge;
    case Fault::FutureTimestamp: return ErrorCode::TimestampOutOfWindow;
    case Fault::StaleTimestamp: return ErrorCode::TimestampOutOfWindow;
    case Fault::UnknownSession: return ErrorCode::SessionForbidden;
    case Fault::UnknownSource: return ErrorCode::SessionForbidden;
    case Fault::WrongVersion: return ErrorCode::UnsupportedVersion;
    case Fault::UnknownField: return ErrorCode::SchemaViolation;
    case Fault::BadId: return ErrorCode::InvalidId;
    default: return ErrorCode::None;
  }
}

static SimConfig simConfig(uint64_t seed) {
  SimConfig sc;
  sc.seed = seed;
  sc.sessions = {{"dream-alpha", {"sim-device-01", "sim-device-02"}}, {"dream-beta", {"sim-device-03"}}};
  return sc;
}

static SessionPolicy simPolicy() {
  SessionPolicy p;
  p.registerSession("dream-alpha", {"sim-device-01", "sim-device-02"});
  p.registerSession("dream-beta", {"sim-device-03"});
  return p;
}

TEST(simulator_every_fault_is_caught_with_the_expected_error) {
  Harness h("sim_each_fault");
  Pipeline p(PipelineConfig{}, simPolicy(), *h.store, h.router, fixedClock());
  Simulator sim(simConfig(5), fixedClock());
  const Fault faults[] = {Fault::BadJson, Fault::MissingConfidence, Fault::ConfidenceTooHigh,
                          Fault::ConfidenceNegative, Fault::ConfidenceWrongType, Fault::UnknownType,
                          Fault::OversizedPayload, Fault::FutureTimestamp, Fault::StaleTimestamp,
                          Fault::UnknownSession, Fault::UnknownSource, Fault::WrongVersion,
                          Fault::UnknownField, Fault::BadId};
  for (int round = 0; round < 20; ++round) {
    for (Fault f : faults) {
      SimEvent se = sim.make(f);
      Outcome o = p.submit(se.raw);
      ++g_checks;
      if (o.status != Outcome::Status::Rejected || o.error.code != expectedError(f)) {
        ++g_failures;
        std::cerr << "  FAIL fault " << toString(f) << " produced status " << int(o.status) << " code "
                  << toString(o.error.code) << ", expected " << toString(expectedError(f)) << "\n";
      }
    }
  }
  CHECK_EQ(h.store->size(), size_t(0));
}

TEST(simulator_end_to_end_invariants_and_determinism) {
  const size_t N = 1500;
  size_t accepted_a = 0;
  std::string log_a;
  for (int run = 0; run < 2; ++run) {
    Harness h(run == 0 ? "sim_e2e_a" : "sim_e2e_b");
    SimConfig sc = simConfig(1234);
    sc.fault_rate = 0.25;
    sc.duplicate_rate = 0.10;
    Pipeline p(PipelineConfig{}, simPolicy(), *h.store, h.router, fixedClock());
    Simulator sim(sc, fixedClock());
    size_t expect_ok = 0, expect_dup = 0, expect_rej = 0;
    std::map<Fault, size_t> seen;
    for (size_t i = 0; i < N; ++i) {
      SimEvent se = sim.next();
      ++seen[se.fault];
      Outcome o = p.submit(se.raw);
      if (se.fault == Fault::None) { ++expect_ok; CHECK(o.status == Outcome::Status::Accepted); }
      else if (se.fault == Fault::Duplicate) { ++expect_dup; CHECK(o.status == Outcome::Status::Duplicate); }
      else {
        ++expect_rej;
        CHECK(o.status == Outcome::Status::Rejected);
        CHECK(o.error.code == expectedError(se.fault));
      }
    }
    const Stats& st = p.stats();
    CHECK_EQ(st.submitted, N);
    CHECK_EQ(st.accepted, expect_ok);
    CHECK_EQ(st.duplicates, expect_dup);
    CHECK_EQ(st.totalRejected(), expect_rej);
    CHECK_EQ(st.accepted + st.duplicates + st.totalRejected() + st.storage_errors, N);
    CHECK_EQ(h.store->size(), expect_ok);
    CHECK_EQ(st.actionable + st.requires_confirmation + st.logged_only, st.accepted);
    CHECK_EQ(h.delivered.size(), st.actionable + st.requires_confirmation);
    CHECK(seen.size() >= 14);  // the run exercised nearly every fault kind
    std::string err;
    auto all = h.store->readAll(err);
    CHECK_EQ(all.size(), expect_ok);
    std::set<std::string> ids;
    for (const auto& r : all) {
      ids.insert(r.event.id);
      // Nothing below the threshold may ever have been routed.
      if (r.event.confidence < 0.8) CHECK(r.disposition == Disposition::LoggedOnly);
      if (r.event.type == EventType::LogoffIntent) CHECK(r.disposition != Disposition::Actionable);
    }
    CHECK_EQ(ids.size(), all.size());  // no duplicate ids in the log
    if (run == 0) {
      accepted_a = st.accepted;
      std::ifstream in(h.dir.file("log.jsonl"));
      std::string line;
      while (std::getline(in, line)) log_a += line.substr(line.find("\"event\":")) + "\n";
    } else {
      CHECK_EQ(st.accepted, accepted_a);
      std::string log_b;
      std::ifstream in(h.dir.file("log.jsonl"));
      std::string line;
      while (std::getline(in, line)) log_b += line.substr(line.find("\"event\":")) + "\n";
      CHECK(log_a == log_b);  // same seed -> identical events
    }
  }
}

// --------------------------------------------------------------------- main
int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  fs::create_directories(tmpRoot());
  int failed_tests = 0, ran = 0;
  for (auto& t : registry()) {
    if (!filter.empty() && t.first.find(filter) == std::string::npos) continue;
    int before = g_failures;
    t.second();
    ++ran;
    bool ok = g_failures == before;
    if (!ok) ++failed_tests;
    std::cout << (ok ? "[ ok ] " : "[FAIL] ") << t.first << "\n";
  }
  std::cout << "\n" << ran << " tests, " << g_checks << " checks, " << g_failures << " failed checks\n";
  return g_failures == 0 ? 0 : 1;
}
