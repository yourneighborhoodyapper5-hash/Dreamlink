# Dreamlink — software foundation (Phase 1)

A simulated event pipeline: fake "sensor-derived" events are validated,
authorized, classified by confidence, written to a durable append-only log,
and routed to session subscribers.

**Dreamlink does not read dreams or share them between people.** Everything
here runs on simulated data. Real sensors will plug in later behind the same
protocol (`docs/PROTOCOL.md`).

## Build and test (Debian 13)

    sudo apt install build-essential        # g++ and make, if not installed
    cd /srv/sam/dreamlink
    make                  # builds build/dreamlink_sim and build/dreamlink_tests
    make test             # runs the unit/integration tests
    make asan             # same tests under AddressSanitizer + UBSan
    make run-sim          # 200 simulated messages -> data/events.jsonl

Everything is written under the project directory (`build/`, `data/`), never
to `/` outside the project. A simulated event is roughly 300 bytes of log.

Optional SQLite backend:

    sudo apt install libsqlite3-dev
    make clean && make SQLITE=1
    build/dreamlink_sim --sqlite data/events.db --events 200

## Simulator options

    build/dreamlink_sim --events 1000 --seed 42 --fault-rate 0.2 --dup-rate 0.1 --verbose

Re-running with the same `--seed` and the same log re-sends the same event
ids, which shows duplicate handling: everything valid comes back "duplicate".

## Layout

    src/json.*          strict JSON parser/writer (no dependencies)
    src/event.*         Event model, UUID v4, timestamps
    src/codec.*         wire format v1: encode, strict decode, validation, limits
    src/store.hpp       append-only log interface
    src/jsonl_store.*   JSON-lines log: fsync, file lock, torn-write recovery
    src/sqlite_store.*  optional SQLite log (append-only enforced by triggers)
    src/pipeline.*      validate -> authorize -> classify -> log -> route
    src/simulator.*     fake events + deliberate faults
    src/sim_main.cpp    dreamlink_sim command
    tests/test_main.cpp tests (no framework needed)
    docs/               PLAN.md, PROTOCOL.md, HARDWARE_REQUIREMENTS.md
Your original `main.cpp` is untouched and is not part of this build.
