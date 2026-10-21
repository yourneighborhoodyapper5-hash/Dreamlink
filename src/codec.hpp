// Wire format v1: encoding, strict decoding, and validation of events.
// See docs/PROTOCOL.md for the schema.
#pragma once
#include <chrono>
#include <string>

#include "event.hpp"
#include "json.hpp"

namespace dreamlink {

enum class ErrorCode {
  None,
  MessageTooLarge,
  MalformedJson,
  SchemaViolation,
  UnsupportedVersion,
  UnknownEventType,
  InvalidId,
  InvalidSessionId,
  InvalidSourceId,
  InvalidConfidence,
  InvalidTimestamp,
  TimestampOutOfWindow,
  PayloadTooLarge,
  InvalidPayload,
  SessionForbidden,
  StorageFailure
};

const char* toString(ErrorCode c);

struct Error {
  ErrorCode code = ErrorCode::None;
  std::string detail;  // never contains payload contents (privacy)
  bool ok() const { return code == ErrorCode::None; }
};

struct Limits {
  size_t max_message_bytes = 8192;
  size_t max_identifier_bytes = 64;  // session_id, source_id, payload keys
  size_t max_payload_entries = 16;
  size_t max_payload_value_bytes = 1024;
  std::chrono::seconds max_future_skew{60};
  std::chrono::seconds max_age{24 * 3600};
};

struct DecodeResult {
  Error error;
  Event event;
  bool ok() const { return error.ok(); }
};

// Static checks only (no clock): size, JSON syntax, schema, field formats,
// confidence range, payload limits.
DecodeResult decodeEvent(const std::string& raw, const Limits& limits);
DecodeResult decodeEventValue(const json::Value& root, const Limits& limits);

// Field-level validation of an already-built Event.
Error validateEvent(const Event& e, const Limits& limits);

// Clock-dependent check, kept separate so it can be tested with a fake clock.
Error checkTimestampWindow(const Event& e, TimePoint now, const Limits& limits);

std::string encodeEvent(const Event& e);  // canonical wire JSON, v1

}  // namespace dreamlink
