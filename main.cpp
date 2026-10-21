// Dreamlink event system: data structures only.
// No validation, storage, sensors, or networking yet.

#include <chrono>
#include <ctime>    // std::time_t, std::gmtime
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <utility>  // std::move

// What happened?
enum class EventType {
    PlayerAction,
    MessageSent,
    LeaveRequested,
};

inline std::string toString(EventType type) {
    switch (type) {
        case EventType::PlayerAction:   return "player_action";
        case EventType::MessageSent:    return "message_sent";
        case EventType::LeaveRequested: return "leave_requested";
    }
    return "unknown";
}

using Timestamp = std::chrono::system_clock::time_point;
using Payload = std::map<std::string, std::string>;

// Random 128-bit hex ID (UUID-like, not a strict UUID).
inline std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(16) << rng()
        << std::setw(16) << rng();
    return out.str();
}

// A record of something that happened in a session.
struct Event {
    std::string id;
    std::string sessionId;
    EventType type;
    double confidence;  // 0.0-1.0; range check comes with validation
    Timestamp timestamp;
    Payload payload;

    Event(std::string sessionId,
          EventType type,
          double confidence = 1.0,
          Payload payload = {})
        : id(generateId()),
          sessionId(std::move(sessionId)),
          type(type),
          confidence(confidence),
          timestamp(std::chrono::system_clock::now()),
          payload(std::move(payload)) {}
};

int main() {
    Event event("session-123", EventType::MessageSent, 1.0, {{"text", "hello"}});

    std::time_t t = std::chrono::system_clock::to_time_t(event.timestamp);
    std::cout << "id:         " << event.id << "\n"
              << "session:    " << event.sessionId << "\n"
              << "type:       " << toString(event.type) << "\n"
              << "confidence: " << event.confidence << "\n"
              << "timestamp:  " << std::put_time(std::gmtime(&t), "%FT%TZ") << "\n";
    for (const auto& [key, value] : event.payload) {
        std::cout << "payload:    " << key << " = " << value << "\n";
    }
    return 0;
}
