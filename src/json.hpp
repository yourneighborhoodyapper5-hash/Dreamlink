// Minimal, strict JSON reader/writer. No external dependencies.
// Strictness is deliberate: this parser sits on the trust boundary
// (sensor side -> server), so it rejects anything ambiguous.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace dreamlink::json {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;  // insertion order kept

  static Value makeNull();
  static Value makeBool(bool b);
  static Value makeNumber(double d);
  static Value makeString(std::string s);
  static Value makeObject();
  static Value makeArray();

  const Value* find(const std::string& key) const;  // objects only
  void set(const std::string& key, Value v);        // replace or append
  bool remove(const std::string& key);
};

// Parses exactly one JSON value (RFC 8259) and nothing else.
// Rejects: trailing data, duplicate object keys, invalid UTF-8, control
// characters in strings, lone surrogates, NUL characters, leading zeros,
// NaN/Infinity, and nesting deeper than max_depth.
bool parse(const std::string& text, Value& out, std::string& error,
           int max_depth = 8);

std::string serialize(const Value& v);
std::string quote(const std::string& s);  // JSON string literal with quotes
std::string numberToString(double d);     // shortest round-trip form
bool isValidUtf8(const std::string& s);

}  // namespace dreamlink::json
