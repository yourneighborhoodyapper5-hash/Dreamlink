#include "simulator.hpp"

#include <chrono>
#include <cmath>

#include "codec.hpp"
#include "json.hpp"

namespace dreamlink {

const char* toString(Fault f) {
  switch (f) {
    case Fault::None: return "none";
    case Fault::Duplicate: return "duplicate";
    case Fault::BadJson: return "bad_json";
    case Fault::MissingConfidence: return "missing_confidence";
    case Fault::ConfidenceTooHigh: return "confidence_too_high";
    case Fault::ConfidenceNegative: return "confidence_negative";
    case Fault::ConfidenceWrongType: return "confidence_wrong_type";
    case Fault::UnknownType: return "unknown_type";
    case Fault::OversizedPayload: return "oversized_payload";
    case Fault::FutureTimestamp: return "future_timestamp";
    case Fault::StaleTimestamp: return "stale_timestamp";
    case Fault::UnknownSession: return "unknown_session";
    case Fault::UnknownSource: return "unknown_source";
    case Fault::WrongVersion: return "wrong_version";
    case Fault::UnknownField: return "unknown_field";
    case Fault::BadId: return "bad_id";
  }
  return "?";
}

namespace {
const Fault kFaults[] = {Fault::BadJson,          Fault::MissingConfidence,
                         Fault::ConfidenceTooHigh, Fault::ConfidenceNegative,
                         Fault::ConfidenceWrongType, Fault::UnknownType,
                         Fault::OversizedPayload,  Fault::FutureTimestamp,
                         Fault::StaleTimestamp,    Fault::UnknownSession,
                         Fault::UnknownSource,     Fault::WrongVersion,
                         Fault::UnknownField,      Fault::BadId};
constexpr size_t kFaultCount = sizeof(kFaults) / sizeof(kFaults[0]);
}  // namespace

Simulator::Simulator(SimConfig cfg, Clock clock)
    : cfg_(std::move(cfg)), clock_(std::move(clock)), rng_(cfg_.seed), ids_(cfg_.seed ^ 0x9E3779B97F4A7C15ULL) {}

double Simulator::uniform() {
  return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);
}

size_t Simulator::pick(size_t n) { return static_cast<size_t>(rng_() % n); }

Event Simulator::makeValidEvent() {
  Event e;
  e.id = ids_.next();
  const SimSession& s = cfg_.sessions[pick(cfg_.sessions.size())];
  e.session_id = s.session_id;
  e.source_id = s.sources[pick(s.sources.size())];

  double t = uniform();
  e.type = t < 0.60 ? EventType::PlayerAction : (t < 0.90 ? EventType::Message : EventType::LogoffIntent);
  e.confidence = std::round(uniform() * 100.0) / 100.0;
  e.timestamp = truncateToMillis(clock_()) - std::chrono::milliseconds(pick(2000));

  static const char* actions[] = {"move", "look", "interact", "wave"};
  static const char* dirs[] = {"north", "south", "east", "west"};
  static const char* texts[] = {"hello", "anyone there?", "following the light", "see you soon"};
  switch (e.type) {
    case EventType::PlayerAction:
      e.payload["action"] = actions[pick(4)];
      e.payload["direction"] = dirs[pick(4)];
      break;
    case EventType::Message:
      e.payload["text"] = texts[pick(4)];
      break;
    case EventType::LogoffIntent:
      e.payload["reason"] = "simulated";
      break;
  }
  return e;
}

SimEvent Simulator::make(Fault f) {
  SimEvent out;
  out.fault = f;

  if (f == Fault::None) {
    out.raw = encodeEvent(makeValidEvent());
    sent_valid_.push_back(out.raw);
    return out;
  }
  if (f == Fault::Duplicate) {
    if (sent_valid_.empty()) return make(Fault::None);
    out.raw = sent_valid_[pick(sent_valid_.size())];
    return out;
  }

  std::string base = encodeEvent(makeValidEvent());
  if (f == Fault::BadJson) {
    out.raw = base.substr(0, base.size() / 2);
    return out;
  }

  json::Value v;
  std::string err;
  json::parse(base, v, err);  // base is always valid JSON
  using json::Value;
  TimePoint now = clock_();
  switch (f) {
    case Fault::MissingConfidence: v.remove("confidence"); break;
    case Fault::ConfidenceTooHigh: v.set("confidence", Value::makeNumber(1.7)); break;
    case Fault::ConfidenceNegative: v.set("confidence", Value::makeNumber(-0.2)); break;
    case Fault::ConfidenceWrongType: v.set("confidence", Value::makeString("0.9")); break;
    case Fault::UnknownType: v.set("type", Value::makeString("TELEPORT")); break;
    case Fault::OversizedPayload: {
      Value p = Value::makeObject();
      p.set("blob", Value::makeString(std::string(2000, 'x')));
      v.set("payload", std::move(p));
      break;
    }
    case Fault::FutureTimestamp:
      v.set("timestamp", Value::makeString(formatTimestamp(now + std::chrono::hours(1))));
      break;
    case Fault::StaleTimestamp:
      v.set("timestamp", Value::makeString(formatTimestamp(now - std::chrono::hours(72))));
      break;
    case Fault::UnknownSession: v.set("session_id", Value::makeString("no-such-session")); break;
    case Fault::UnknownSource: v.set("source_id", Value::makeString("rogue-device")); break;
    case Fault::WrongVersion: v.set("v", Value::makeNumber(2)); break;
    case Fault::UnknownField: v.set("debug", Value::makeBool(true)); break;
    case Fault::BadId: v.set("id", Value::makeString("12345")); break;
    default: break;
  }
  out.raw = json::serialize(v);
  return out;
}

SimEvent Simulator::next() {
  double r = uniform();
  if (r < cfg_.fault_rate) return make(kFaults[pick(kFaultCount)]);
  if (r < cfg_.fault_rate + cfg_.duplicate_rate) return make(Fault::Duplicate);
  return make(Fault::None);
}

}  // namespace dreamlink
