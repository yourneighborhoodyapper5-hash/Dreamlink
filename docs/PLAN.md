# Implementation plan and milestones

Status legend: DONE = implemented and tested here; NEXT = not started.

## Review of the original prototype (main.cpp)
Based on the handoff description; the file itself was not available when
this was written, so check these against the real code.
- `generateId()` is not a UUID. Replaced by RFC 4122 v4 in `src/event.cpp`
  (`UuidGenerator`). If the old one builds a new `std::mt19937` from
  `std::random_device` on every call, that is slow and wasteful; the new
  generator seeds once.
- Confidence was unvalidated. Now must be a finite number in [0, 1].
- `std::map<string,string>` payload is fine for now; limits are enforced
  (16 entries, 1 KiB values).
- `system_clock` is correct for wall-clock event times. Timestamps are
  UTC with millisecond precision on the wire.
- Nothing was persisted or transmitted. Now persisted; transmission is Phase 2.

## Phase 1 — software foundation
| # | Milestone | State |
|---|-----------|-------|
| 1 | Event model, UUID v4, ms timestamps | DONE |
| 2 | Wire format v1, strict decoder, limits (docs/PROTOCOL.md) | DONE |
| 3 | Tests: creation, serialization, validation, malformed input | DONE |
| 4 | Simulator with 14 kinds of deliberate faults | DONE |
| 5 | Append-only log, unique ids, torn-write recovery, single writer | DONE (JSONL tested) |
| 6 | SQLite backend (UNIQUE id, append-only triggers) | WRITTEN, NOT YET RUN: needs libsqlite3-dev |
| 7 | Confidence policy: below threshold is logged, never acted on | DONE |
| 8 | LOGOFF_INTENT only ever requests confirmation | DONE |

## Known limitations (be aware, not hidden)
- Routing is in-process and not persisted. If the process dies after an
  event is logged but before it is routed, that routing is not replayed.
  Phase 2 adds per-event delivery status.
- Duplicate detection is by id only. A reused id with different content is
  acknowledged as a duplicate and the first version is kept.
- The JSONL store keeps all ids in memory and re-reads the file on open.
  Fine for experiments; SQLite is the path for larger logs.
- No encryption at rest and no authentication yet (Phase 2). Log files are
  created mode 0600.
- Deleting events (user deletion rights) is deliberately not implemented in
  the append-only log; it needs an explicit, audited admin path in Phase 3.
- Simulated confidence is uniform random, so most simulated events are
  below the 0.80 threshold. That is the simulator, not a finding.

## Phase 2 — device communication (NEXT)
1. Local transport (Unix socket or TCP on localhost) carrying the v1 messages,
   one JSON object per line.
2. Device client library plus server receiver; TLS or a Unix socket with file
   permissions; per-device keys.
3. Acknowledgment = the event is fsynced. Client retries until acked; the
   server's duplicate handling makes retries safe.
4. Delivery status table and replay of un-routed events after restart.
5. Hardware-less testing with the simulator as the "device".

## Phase 3 and later
As in the project plan: accounts, session browser, consent and deletion
controls, retention rules. Do not start before Phase 2 acks and delivery
status work.
