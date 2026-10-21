#include "event.hpp"

#include <cstdio>

namespace dreamlink {

TimePoint systemNow() { return std::chrono::system_clock::now(); }

int64_t toEpochMillis(TimePoint tp) {
  auto d = tp.time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
  if (ms > d) ms -= std::chrono::milliseconds(1);  // floor for pre-1970 values
  return ms.count();
}

TimePoint fromEpochMillis(int64_t ms) {
  return TimePoint(std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::milliseconds(ms)));
}

TimePoint truncateToMillis(TimePoint tp) { return fromEpochMillis(toEpochMillis(tp)); }

const char* toWire(EventType t) {
  switch (t) {
    case EventType::PlayerAction: return "PLAYER_ACTION";
    case EventType::Message: return "MESSAGE";
    case EventType::LogoffIntent: return "LOGOFF_INTENT";
  }
  return "UNKNOWN";
}

bool eventTypeFromWire(const std::string& s, EventType& out) {
  if (s == "PLAYER_ACTION") { out = EventType::PlayerAction; return true; }
  if (s == "MESSAGE") { out = EventType::Message; return true; }
  if (s == "LOGOFF_INTENT") { out = EventType::LogoffIntent; return true; }
  return false;
}

UuidGenerator::UuidGenerator() {
  std::random_device rd;
  std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
  rng_.seed(seq);
}

UuidGenerator::UuidGenerator(uint64_t seed) : rng_(seed) {}

std::string UuidGenerator::next() {
  uint64_t hi = rng_(), lo = rng_();
  uint8_t b[16];
  for (int i = 0; i < 8; ++i) {
    b[i] = static_cast<uint8_t>(hi >> (8 * (7 - i)));
    b[8 + i] = static_cast<uint8_t>(lo >> (8 * (7 - i)));
  }
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);  // version 4
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);  // RFC 4122 variant
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
    out.push_back(hex[b[i] >> 4]);
    out.push_back(hex[b[i] & 0x0F]);
  }
  return out;
}

bool isUuidV4(const std::string& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < 36; ++i) {
    char c = s[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  if (s[14] != '4') return false;
  char v = s[19];
  return v == '8' || v == '9' || v == 'a' || v == 'b';
}

namespace {

// Howard Hinnant's civil-date algorithms (public domain).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += (m <= 2);
}

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

}  // namespace

std::string formatTimestamp(TimePoint tp) {
  int64_t ms = toEpochMillis(tp);
  int64_t secs = floorDiv(ms, 1000);
  int rem_ms = static_cast<int>(ms - secs * 1000);
  int64_t days = floorDiv(secs, 86400);
  int64_t sod = secs - days * 86400;
  int64_t y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  char buf[48];
  std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ",
                static_cast<long long>(y), m, d, static_cast<int>(sod / 3600),
                static_cast<int>((sod % 3600) / 60), static_cast<int>(sod % 60), rem_ms);
  return buf;
}

bool parseTimestamp(const std::string& s, TimePoint& out) {
  if (s.size() != 24) return false;
  if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' ||
      s[19] != '.' || s[23] != 'Z')
    return false;
  auto digits = [&](size_t pos, size_t n, int& v) {
    v = 0;
    for (size_t k = pos; k < pos + n; ++k) {
      if (s[k] < '0' || s[k] > '9') return false;
      v = v * 10 + (s[k] - '0');
    }
    return true;
  };
  int Y, M, D, h, mi, sec, ms;
  if (!digits(0, 4, Y) || !digits(5, 2, M) || !digits(8, 2, D) || !digits(11, 2, h) ||
      !digits(14, 2, mi) || !digits(17, 2, sec) || !digits(20, 3, ms))
    return false;
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || sec > 59) return false;
  int64_t days = daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D));
  int64_t total = (((days * 24 + h) * 60 + mi) * 60 + sec) * 1000 + ms;
  TimePoint tp = fromEpochMillis(total);
  if (formatTimestamp(tp) != s) return false;  // rejects impossible dates (Feb 30...)
  out = tp;
  return true;
}

}  // namespace dreamlink
