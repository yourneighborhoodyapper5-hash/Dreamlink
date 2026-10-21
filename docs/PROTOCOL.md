# Dreamlink event protocol, version 1

One event is one UTF-8 JSON object, at most 8192 bytes. Over a stream, send
one object per line. The server validates everything and rejects the whole
message on the first problem; there is no partial acceptance.

## Fields

| Field | Type | Required | Rules |
|-------|------|----------|-------|
| `v` | number | yes | must be `1` |
| `id` | string | yes | lowercase UUID v4, unique per event; the sender reuses it when retrying |
| `session_id` | string | yes | `[A-Za-z0-9_-]{1,64}`; must be a session the server knows |
| `type` | string | yes | `PLAYER_ACTION`, `MESSAGE`, or `LOGOFF_INTENT` |
| `confidence` | number | yes | finite, 0 to 1 inclusive. A score, not proof |
| `timestamp` | string | yes | `YYYY-MM-DDTHH:MM:SS.mmmZ` (UTC, exactly this form); at most 60 s in the future, at most 24 h old |
| `payload` | object | yes | at most 16 entries; keys `[A-Za-z0-9_.-]{1,64}`; values are strings of at most 1024 bytes |
| `source_id` | string | no | device id, `[A-Za-z0-9_-]{1,64}`; if the session lists permitted devices it is required and must be on the list |

Unknown top-level fields are rejected. Duplicate keys, NaN/Infinity, invalid
UTF-8, trailing data, and nesting deeper than 8 levels are rejected.

## Example

    {"v":1,"id":"bf7adec3-95fe-4c8c-9781-cf75650ace1f","session_id":"dream-alpha",
     "type":"PLAYER_ACTION","confidence":0.9,"timestamp":"2026-10-21T19:44:44.536Z",
     "payload":{"action":"move","direction":"north"},"source_id":"sim-device-01"}

(shown wrapped; send it on one line)

## Server decisions

Accepted events get a disposition, stored with the event:

- `actionable`: confidence at or above the threshold (default 0.80); may be routed.
- `requires_confirmation`: a `LOGOFF_INTENT` at or above the threshold. Routed only
  as a request; a person or manual control decides. Never ends a session by itself.
- `logged_only`: below the threshold. Kept for research, never routed.

Outcomes returned to the sender: accepted, duplicate (treat as success; do not
resend), rejected with an error code (do not retry unchanged), or storage error
(retry with the same `id`).

Error codes: `message_too_large`, `malformed_json`, `schema_violation`,
`unsupported_version`, `unknown_event_type`, `invalid_id`, `invalid_session_id`,
`invalid_source_id`, `invalid_confidence`, `invalid_timestamp`,
`timestamp_out_of_window`, `payload_too_large`, `invalid_payload`,
`session_forbidden`, `storage_failure`. Error text never echoes payload contents.

## Versioning
Breaking changes bump `v`. A server rejects versions it does not know.

## Log record (JSONL backend)
    {"received_at":"...","disposition":"actionable","event":{ ...the v1 event... }}
