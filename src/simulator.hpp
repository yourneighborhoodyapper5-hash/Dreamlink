// Fake "sensor-derived" event generator. Produces valid events plus
// deliberately broken ones so every validation rule gets exercised.
// Nothing here reads real sensors or models real physiology.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "event.hpp"

namespace dreamlink {

enum class Fault {
  None,
  Duplicate,
  BadJson,
  MissingConfidence,
  ConfidenceTooHigh,
  ConfidenceNegative,
  ConfidenceWrongType,
  UnknownType,
  OversizedPayload,
  FutureTimestamp,
  StaleTimestamp,
  UnknownSession,
  UnknownSource,
  WrongVersion,
  UnknownField,
  BadId
};

const char* toString(Fault f);

struct SimSession {
  std::string session_id;
  std::vector<std::string> sources;  // device ids that may send into it
};

struct SimEvent {
  std::string raw;  // what goes "over the wire"
  Fault fault = Fault::None;
};

struct SimConfig {
  uint64_t seed = 1;
  double fault_rate = 0.15;      // fraction of messages that are malformed
  double duplicate_rate = 0.05;  // fraction that re-send an earlier message
  std::vector<SimSession> sessions;
};

class Simulator {
 public:
  Simulator(SimConfig cfg, Clock clock);
  SimEvent next();
  SimEvent make(Fault f);  // force a specific fault (used by tests)

 private:
  double uniform();
  size_t pick(size_t n);
  Event makeValidEvent();
  SimConfig cfg_;
  Clock clock_;
  std::mt19937_64 rng_;
  UuidGenerator ids_;
  std::vector<std::string> sent_valid_;
};

}  // namespace dreamlink
