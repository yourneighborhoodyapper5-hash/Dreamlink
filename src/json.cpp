#include "json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace dreamlink::json {

Value Value::makeNull() { return Value(); }
Value Value::makeBool(bool b) { Value v; v.type = Type::Bool; v.boolean = b; return v; }
Value Value::makeNumber(double d) { Value v; v.type = Type::Number; v.number = d; return v; }
Value Value::makeString(std::string s) { Value v; v.type = Type::String; v.string = std::move(s); return v; }
Value Value::makeObject() { Value v; v.type = Type::Object; return v; }
Value Value::makeArray() { Value v; v.type = Type::Array; return v; }

const Value* Value::find(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& kv : object)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

void Value::set(const std::string& key, Value v) {
  for (auto& kv : object) {
    if (kv.first == key) { kv.second = std::move(v); return; }
  }
  object.emplace_back(key, std::move(v));
}

bool Value::remove(const std::string& key) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (it->first == key) { object.erase(it); return true; }
  }
  return false;
}

bool isValidUtf8(const std::string& s) {
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; continue; }
    int len;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + static_cast<size_t>(len) > n) return false;
    for (int k = 1; k < len; ++k) {
      unsigned char d = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
      if ((d & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (d & 0x3F);
    }
    if (len == 2 && cp < 0x80) return false;       // overlong
    if (len == 3 && cp < 0x800) return false;      // overlong
    if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // surrogates
    i += static_cast<size_t>(len);
  }
  return true;
}

namespace {

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

class Parser {
 public:
  Parser(const std::string& s, int max_depth) : s_(s), max_depth_(max_depth) {}

  bool run(Value& out, std::string& error) {
    if (!isValidUtf8(s_)) { error = "invalid UTF-8"; return false; }
    skipWs();
    if (!parseValue(out, 0)) { error = err_; return false; }
    skipWs();
    if (pos_ != s_.size()) {
      error = "unexpected data after JSON value";
      return false;
    }
    return true;
  }

 private:
  const std::string& s_;
  int max_depth_;
  size_t pos_ = 0;
  std::string err_;

  bool fail(const char* msg) {
    err_ = std::string(msg) + " at byte " + std::to_string(pos_);
    return false;
  }
  char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
  static bool isDigit(char c) { return c >= '0' && c <= '9'; }
  void skipWs() {
    while (pos_ < s_.size()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
      else break;
    }
  }

  bool parseValue(Value& out, int depth) {
    if (depth > max_depth_) return fail("nesting too deep");
    if (pos_ >= s_.size()) return fail("unexpected end of input");
    char c = peek();
    if (c == '{') return parseObject(out, depth);
    if (c == '[') return parseArray(out, depth);
    if (c == '"') {
      out = Value::makeString("");
      return parseString(out.string);
    }
    if (c == 't') return literal("true", out, Value::makeBool(true));
    if (c == 'f') return literal("false", out, Value::makeBool(false));
    if (c == 'n') return literal("null", out, Value::makeNull());
    if (c == '-' || isDigit(c)) return parseNumber(out);
    return fail("unexpected character");
  }

  bool literal(const char* word, Value& out, Value v) {
    size_t n = std::char_traits<char>::length(word);
    if (s_.compare(pos_, n, word) != 0) return fail("invalid literal");
    pos_ += n;
    out = std::move(v);
    return true;
  }

  bool parseNumber(Value& out) {
    size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (peek() == '0') {
      ++pos_;
    } else if (peek() >= '1' && peek() <= '9') {
      while (isDigit(peek())) ++pos_;
    } else {
      return fail("invalid number");
    }
    if (peek() == '.') {
      ++pos_;
      if (!isDigit(peek())) return fail("invalid number");
      while (isDigit(peek())) ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') ++pos_;
      if (!isDigit(peek())) return fail("invalid number");
      while (isDigit(peek())) ++pos_;
    }
    double d = 0.0;
    auto res = std::from_chars(s_.data() + start, s_.data() + pos_, d);
    if (res.ec != std::errc() || res.ptr != s_.data() + pos_ || !std::isfinite(d))
      return fail("number out of range");
    out = Value::makeNumber(d);
    return true;
  }

  bool hex4(uint32_t& cp) {
    if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
    cp = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_++];
      cp <<= 4;
      if (c >= '0' && c <= '9') cp |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') cp |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') cp |= static_cast<uint32_t>(c - 'A' + 10);
      else return fail("invalid \\u escape");
    }
    return true;
  }

  bool parseString(std::string& out) {
    ++pos_;  // opening quote
    for (;;) {
      if (pos_ >= s_.size()) return fail("unterminated string");
      unsigned char c = static_cast<unsigned char>(s_[pos_++]);
      if (c == '"') return true;
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
      if (pos_ >= s_.size()) return fail("unterminated escape");
      char e = s_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
              pos_ += 2;
              uint32_t lo = 0;
              if (!hex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) return fail("invalid surrogate pair");
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              return fail("lone surrogate");
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("lone surrogate");
          }
          if (cp == 0) return fail("NUL character not allowed");
          appendUtf8(out, cp);
          break;
        }
        default: return fail("invalid escape");
      }
    }
  }

  bool parseArray(Value& out, int depth) {
    out = Value::makeArray();
    ++pos_;
    skipWs();
    if (peek() == ']') { ++pos_; return true; }
    for (;;) {
      Value item;
      skipWs();
      if (!parseValue(item, depth + 1)) return false;
      out.array.push_back(std::move(item));
      skipWs();
      if (peek() == ',') { ++pos_; continue; }
      if (peek() == ']') { ++pos_; return true; }
      return fail("expected ',' or ']'");
    }
  }

  bool parseObject(Value& out, int depth) {
    out = Value::makeObject();
    ++pos_;
    skipWs();
    if (peek() == '}') { ++pos_; return true; }
    for (;;) {
      skipWs();
      if (peek() != '"') return fail("expected string key");
      std::string key;
      if (!parseString(key)) return false;
      if (out.find(key) != nullptr) return fail("duplicate key");
      skipWs();
      if (peek() != ':') return fail("expected ':'");
      ++pos_;
      skipWs();
      Value item;
      if (!parseValue(item, depth + 1)) return false;
      out.object.emplace_back(std::move(key), std::move(item));
      skipWs();
      if (peek() == ',') { ++pos_; continue; }
      if (peek() == '}') { ++pos_; return true; }
      return fail("expected ',' or '}'");
    }
  }
};

}  // namespace

bool parse(const std::string& text, Value& out, std::string& error, int max_depth) {
  Parser p(text, max_depth);
  return p.run(out, error);
}

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

std::string numberToString(double d) {
  if (!std::isfinite(d)) return "null";  // JSON has no NaN/Infinity
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, d);
  return std::string(buf, res.ptr);
}

std::string serialize(const Value& v) {
  switch (v.type) {
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return v.boolean ? "true" : "false";
    case Value::Type::Number: return numberToString(v.number);
    case Value::Type::String: return quote(v.string);
    case Value::Type::Array: {
      std::string out = "[";
      for (size_t i = 0; i < v.array.size(); ++i) {
        if (i) out += ",";
        out += serialize(v.array[i]);
      }
      return out + "]";
    }
    case Value::Type::Object: {
      std::string out = "{";
      for (size_t i = 0; i < v.object.size(); ++i) {
        if (i) out += ",";
        out += quote(v.object[i].first) + ":" + serialize(v.object[i].second);
      }
      return out + "}";
    }
  }
  return "null";
}

}  // namespace dreamlink::json
