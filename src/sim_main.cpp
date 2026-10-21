// dreamlink_sim: runs simulated sensor events through the full pipeline
// (validate -> authorize -> classify -> durable log -> route) and reports.
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <set>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "jsonl_store.hpp"
#include "pipeline.hpp"
#include "simulator.hpp"
#ifdef DREAMLINK_WITH_SQLITE
#include "sqlite_store.hpp"
#endif

using namespace dreamlink;

namespace {

void usage() {
  std::cout <<
      "Usage: dreamlink_sim [options]\n"
      "  --events N          messages to simulate (default 200)\n"
      "  --seed S            RNG seed (default: current time; printed)\n"
      "  --log PATH          JSONL event log (default data/events.jsonl)\n"
      "  --sqlite PATH       use a SQLite database instead (needs `make SQLITE=1`)\n"
      "  --fault-rate R      fraction of malformed messages (default 0.15)\n"
      "  --dup-rate R        fraction of re-sent messages (default 0.05)\n"
      "  --threshold T       confidence needed to act on an event (default 0.80)\n"
      "  --no-fsync          faster, but loses the durability guarantee\n"
      "  --verbose           print every outcome\n"
      "Simulated data only. No real sensor data is read or implied.\n";
}

bool parseU64(const char* s, uint64_t& out) {
  char* end = nullptr;
  errno = 0;
  unsigned long long v = std::strtoull(s, &end, 10);
  if (errno || end == s || *end != '\0') return false;
  out = v;
  return true;
}

bool parseRate(const char* s, double& out) {
  char* end = nullptr;
  double v = std::strtod(s, &end);
  if (end == s || *end != '\0' || !(v >= 0.0 && v <= 1.0)) return false;
  out = v;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t events = 200;
  uint64_t seed = static_cast<uint64_t>(std::time(nullptr));
  std::string log_path = "data/events.jsonl";
  std::string sqlite_path;
  double fault_rate = 0.15, dup_rate = 0.05, threshold = 0.80;
  bool fsync_each = true, verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << what << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") { usage(); return 0; }
    else if (a == "--events") { if (!parseU64(need("--events"), events)) { std::cerr << "bad --events\n"; return 2; } }
    else if (a == "--seed") { if (!parseU64(need("--seed"), seed)) { std::cerr << "bad --seed\n"; return 2; } }
    else if (a == "--log") log_path = need("--log");
    else if (a == "--sqlite") sqlite_path = need("--sqlite");
    else if (a == "--fault-rate") { if (!parseRate(need("--fault-rate"), fault_rate)) { std::cerr << "bad --fault-rate (0..1)\n"; return 2; } }
    else if (a == "--dup-rate") { if (!parseRate(need("--dup-rate"), dup_rate)) { std::cerr << "bad --dup-rate (0..1)\n"; return 2; } }
    else if (a == "--threshold") { if (!parseRate(need("--threshold"), threshold)) { std::cerr << "bad --threshold (0..1)\n"; return 2; } }
    else if (a == "--no-fsync") fsync_each = false;
    else if (a == "--verbose") verbose = true;
    else { std::cerr << "unknown option: " << a << "\n"; usage(); return 2; }
  }
  if (fault_rate + dup_rate > 1.0) { std::cerr << "fault-rate + dup-rate must be <= 1\n"; return 2; }

  std::unique_ptr<EventStore> store;
  std::string err;
  size_t recovered = 0;
  if (!sqlite_path.empty()) {
#ifdef DREAMLINK_WITH_SQLITE
    store = SqliteEventStore::open(sqlite_path, err);
#else
    std::cerr << "This build has no SQLite support. Rebuild with: make SQLITE=1\n";
    return 2;
#endif
  } else {
    store = JsonlEventStore::open(log_path, fsync_each, err, &recovered);
  }
  if (!store) { std::cerr << "cannot open event log: " << err << "\n"; return 1; }
  if (recovered) std::cout << "recovered: dropped " << recovered << " bytes of a torn final record\n";
  const size_t before = store->size();

  // Two simulated sessions, each with its own allowed (simulated) devices.
  SimConfig sc;
  sc.seed = seed;
  sc.fault_rate = fault_rate;
  sc.duplicate_rate = dup_rate;
  sc.sessions = {{"dream-alpha", {"sim-device-01", "sim-device-02"}},
                 {"dream-beta", {"sim-device-03"}}};

  SessionPolicy policy;
  for (const auto& s : sc.sessions)
    policy.registerSession(s.session_id, std::set<std::string>(s.sources.begin(), s.sources.end()));

  std::map<std::string, size_t> delivered_per_session;
  SessionRouter router;
  for (const auto& s : sc.sessions) {
    router.subscribe(s.session_id, [&delivered_per_session, verbose](const Event& e, Disposition d) {
      ++delivered_per_session[e.session_id];
      if (verbose)
        std::cout << "  -> delivered " << toWire(e.type) << " to session " << e.session_id << " ("
                  << toString(d) << ")\n";
    });
  }

  PipelineConfig pc;
  pc.action_threshold = threshold;
  Pipeline pipeline(pc, policy, *store, router);
  Simulator sim(sc, systemNow);

  std::cout << "simulating " << events << " messages, seed " << seed << "\n";
  size_t storage_errors = 0;
  for (uint64_t i = 0; i < events; ++i) {
    SimEvent se = sim.next();
    Outcome o = pipeline.submit(se.raw);
    if (verbose) {
      std::cout << "#" << i << " [" << toString(se.fault) << "] ";
      switch (o.status) {
        case Outcome::Status::Accepted: std::cout << "accepted (" << toString(o.disposition) << ")\n"; break;
        case Outcome::Status::Duplicate: std::cout << "duplicate\n"; break;
        case Outcome::Status::Rejected: std::cout << "rejected: " << toString(o.error.code) << " - " << o.error.detail << "\n"; break;
        case Outcome::Status::StorageError: std::cout << "STORAGE ERROR: " << o.error.detail << "\n"; break;
      }
    }
    if (o.status == Outcome::Status::StorageError) ++storage_errors;
  }

  const Stats& st = pipeline.stats();
  std::cout << "\n--- summary ---\n"
            << "submitted:             " << st.submitted << "\n"
            << "accepted & logged:     " << st.accepted << "\n"
            << "  actionable:          " << st.actionable << "\n"
            << "  requires confirm:    " << st.requires_confirmation << "\n"
            << "  logged only (low confidence): " << st.logged_only << "\n"
            << "duplicates ignored:    " << st.duplicates << "\n"
            << "rejected:              " << st.totalRejected() << "\n";
  for (const auto& kv : st.rejected) std::cout << "  " << toString(kv.first) << ": " << kv.second << "\n";
  std::cout << "storage errors:        " << st.storage_errors << "\n"
            << "deliveries to clients: " << st.deliveries << "\n";
  for (const auto& kv : delivered_per_session) std::cout << "  " << kv.first << ": " << kv.second << "\n";

  // Self-check: the durable log must contain exactly the accepted events.
  std::string rerr;
  size_t after = store->readAll(rerr).size();
  if (!rerr.empty()) { std::cerr << "VERIFY FAILED: cannot re-read log: " << rerr << "\n"; return 1; }
  bool ok = (after == before + st.accepted) && storage_errors == 0;
  std::cout << "\nlog records: " << before << " before, " << after << " after (+" << st.accepted
            << " accepted) -> " << (ok ? "VERIFY OK" : "VERIFY FAILED") << "\n";
  return ok ? 0 : 1;
}
