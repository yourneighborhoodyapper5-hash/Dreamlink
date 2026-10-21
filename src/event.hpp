// Core event model for Dreamlink (wire protocol v1).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>

namespace dreamlink {

using TimePoint = std::chrono::system_clock::time_point;
using Clock = std::function<TimePoint()>;
using Payload = std::map<std::string, std::string>;

TimePoint systemNow();
int64_t toEpochMillis(TimePoint tp);
TimePoint fromEpochMillis(int64_t ms);
TimePoint truncateToMillis(TimePoint tp);

enum class EventType { PlayerAction, Message, LogoffIntent };

const char* toWire(EventType t);                                  // "PLAYER_ACTION", ...
bool eventTypeFromWire(const std::string& s, EventType& out);

struct Event {
  std::string id;          // lowercase UUID v4
  std::string session_id;  // [A-Za-z0-9_-]{1,64}
  EventType type = EventType::Message;
  double confidence = 0.0;  // [0,1]; a score from the interpreter, NOT proof
  TimePoint timestamp{};    // when the sensor side says it happened (UTC, ms)
  Payload payload;
  std::optional<std::string> source_id;  // device/source identifier
};

// RFC 4122 version-4 UUIDs. The seeded constructor exists only so the
// simulator and tests are reproducible; the default constructor seeds
// from std::random_device.
class UuidGenerator {
 public:
  UuidGenerator();
  explicit UuidGenerator(uint64_t seed);
  std::string next();

 private:
  std::mt19937_64 rng_;
};

bool isUuidV4(const std::string& s);  // lowercase canonical form only

// "YYYY-MM-DDTHH:MM:SS.mmmZ" (UTC, millisecond precision, exactly this form).
std::string formatTimestamp(TimePoint tp);
bool parseTimestamp(const std::string& s, TimePoint& out);

}  // namespace dreamlink
