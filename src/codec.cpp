#include "codec.hpp"

#include <cmath>
#include <set>

namespace dreamlink {

const char* toString(ErrorCode c) {
  switch (c) {
    case ErrorCode::None: return "none";
    case ErrorCode::MessageTooLarge: return "message_too_large";
    case ErrorCode::MalformedJson: return "malformed_json";
    case ErrorCode::SchemaViolation: return "schema_violation";
    case ErrorCode::UnsupportedVersion: return "unsupported_version";
    case ErrorCode::UnknownEventType: return "unknown_event_type";
    case ErrorCode::InvalidId: return "invalid_id";
    case ErrorCode::InvalidSessionId: return "invalid_session_id";
    case ErrorCode::InvalidSourceId: return "invalid_source_id";
    case ErrorCode::InvalidConfidence: return "invalid_confidence";
    case ErrorCode::InvalidTimestamp: return "invalid_timestamp";
    case ErrorCode::TimestampOutOfWindow: return "timestamp_out_of_window";
    case ErrorCode::PayloadTooLarge: return "payload_too_large";
    case ErrorCode::InvalidPayload: return "invalid_payload";
    case ErrorCode::SessionForbidden: return "session_forbidden";
    case ErrorCode::StorageFailure: return "storage_failure";
  }
  return "unknown";
}

namespace {

std::string clip(const std::string& s, size_t n = 40) {
  return s.size() <= n ? s : s.substr(0, n) + "...";
}

bool isIdentifier(const std::string& s, size_t max_len, bool allow_dot) {
  if (s.empty() || s.size() > max_len) return false;
  for (unsigned char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || (allow_dot && c == '.');
    if (!ok) return false;
  }
  return true;
}

DecodeResult fail(ErrorCode code, std::string detail) {
  DecodeResult r;
  r.error = {code, std::move(detail)};
  return r;
}

}  // namespace

Error validateEvent(const Event& e, const Limits& lim) {
  if (!isUuidV4(e.id)) return {ErrorCode::InvalidId, "id must be a lowercase UUID v4"};
  if (!isIdentifier(e.session_id, lim.max_identifier_bytes, false))
    return {ErrorCode::InvalidSessionId, "session_id must match [A-Za-z0-9_-]{1,64}"};
  if (e.source_id && !isIdentifier(*e.source_id, lim.max_identifier_bytes, false))
    return {ErrorCode::InvalidSourceId, "source_id must match [A-Za-z0-9_-]{1,64}"};
  if (!std::isfinite(e.confidence) || e.confidence < 0.0 || e.confidence > 1.0)
    return {ErrorCode::InvalidConfidence, "confidence must be a number in [0, 1]"};
  if (e.payload.size() > lim.max_payload_entries)
    return {ErrorCode::PayloadTooLarge, "too many payload entries"};
  for (const auto& kv : e.payload) {
    if (!isIdentifier(kv.first, lim.max_identifier_bytes, true))
      return {ErrorCode::InvalidPayload, "payload key must match [A-Za-z0-9_.-]{1,64}"};
    if (kv.second.size() > lim.max_payload_value_bytes)
      return {ErrorCode::PayloadTooLarge, "payload value too large"};
    if (!json::isValidUtf8(kv.second))
      return {ErrorCode::InvalidPayload, "payload value is not valid UTF-8"};
  }
  return {};
}

Error checkTimestampWindow(const Event& e, TimePoint now, const Limits& lim) {
  if (e.timestamp > now + lim.max_future_skew)
    return {ErrorCode::TimestampOutOfWindow, "timestamp is too far in the future"};
  if (e.timestamp < now - lim.max_age)
    return {ErrorCode::TimestampOutOfWindow, "timestamp is too old"};
  return {};
}

DecodeResult decodeEventValue(const json::Value& root, const Limits& lim) {
  using T = json::Value::Type;
  if (root.type != T::Object)
    return fail(ErrorCode::SchemaViolation, "top-level value must be an object");

  static const std::set<std::string> known = {"v",         "id",      "session_id", "type",
                                               "confidence", "timestamp", "payload",    "source_id"};
  for (const auto& kv : root.object) {
    if (!known.count(kv.first))
      return fail(ErrorCode::SchemaViolation, "unknown field: " + clip(kv.first));
  }

  auto require = [&](const char* name, T type, const json::Value*& out) -> bool {
    out = root.find(name);
    return out != nullptr && out->type == type;
  };

  const json::Value* v = nullptr;
  if (!require("v", T::Number, v))
    return fail(ErrorCode::SchemaViolation, "field 'v' must be a number");
  if (v->number != 1.0)
    return fail(ErrorCode::UnsupportedVersion, "unsupported protocol version");

  const json::Value *id, *session, *type, *conf, *ts, *payload;
  if (!require("id", T::String, id))
    return fail(ErrorCode::SchemaViolation, "field 'id' must be a string");
  if (!require("session_id", T::String, session))
    return fail(ErrorCode::SchemaViolation, "field 'session_id' must be a string");
  if (!require("type", T::String, type))
    return fail(ErrorCode::SchemaViolation, "field 'type' must be a string");
  if (!require("confidence", T::Number, conf))
    return fail(ErrorCode::SchemaViolation, "field 'confidence' must be a number");
  if (!require("timestamp", T::String, ts))
    return fail(ErrorCode::SchemaViolation, "field 'timestamp' must be a string");
  if (!require("payload", T::Object, payload))
    return fail(ErrorCode::SchemaViolation, "field 'payload' must be an object");

  DecodeResult r;
  Event& e = r.event;
  e.id = id->string;
  e.session_id = session->string;
  e.confidence = conf->number;

  if (!eventTypeFromWire(type->string, e.type))
    return fail(ErrorCode::UnknownEventType, "unknown event type: " + clip(type->string));

  if (!parseTimestamp(ts->string, e.timestamp))
    return fail(ErrorCode::InvalidTimestamp,
                "timestamp must look like 2026-01-31T12:00:00.000Z");

  if (const json::Value* src = root.find("source_id")) {
    if (src->type != T::String)
      return fail(ErrorCode::SchemaViolation, "field 'source_id' must be a string");
    e.source_id = src->string;
  }

  for (const auto& kv : payload->object) {
    if (kv.second.type != T::String)
      return fail(ErrorCode::SchemaViolation, "payload values must be strings");
    e.payload.emplace(kv.first, kv.second.string);
  }

  r.error = validateEvent(e, lim);
  return r;
}

DecodeResult decodeEvent(const std::string& raw, const Limits& lim) {
  if (raw.size() > lim.max_message_bytes)
    return fail(ErrorCode::MessageTooLarge, "message exceeds size limit");
  json::Value root;
  std::string err;
  if (!json::parse(raw, root, err)) return fail(ErrorCode::MalformedJson, err);
  return decodeEventValue(root, lim);
}

std::string encodeEvent(const Event& e) {
  std::string out = "{\"v\":1,\"id\":" + json::quote(e.id) +
                    ",\"session_id\":" + json::quote(e.session_id) +
                    ",\"type\":" + json::quote(toWire(e.type)) +
                    ",\"confidence\":" + json::numberToString(e.confidence) +
                    ",\"timestamp\":" + json::quote(formatTimestamp(e.timestamp)) +
                    ",\"payload\":{";
  bool first = true;
  for (const auto& kv : e.payload) {
    if (!first) out += ",";
    first = false;
    out += json::quote(kv.first) + ":" + json::quote(kv.second);
  }
  out += "}";
  if (e.source_id) out += ",\"source_id\":" + json::quote(*e.source_id);
  out += "}";
  return out;
}

}  // namespace dreamlink
