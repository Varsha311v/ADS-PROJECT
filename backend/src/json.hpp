#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ff::json {
struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> obj;

  static Value null() { return Value{}; }
  static Value boolean(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
  static Value number(double v) { Value x; x.type = Type::Number; x.num = v; return x; }
  static Value string(std::string v) { Value x; x.type = Type::String; x.str = std::move(v); return x; }
  static Value array() { Value x; x.type = Type::Array; return x; }
  static Value object() { Value x; x.type = Type::Object; return x; }
  bool isString() const { return type == Type::String; }
  bool isArray() const { return type == Type::Array; }
  bool isObject() const { return type == Type::Object; }
  const Value* find(const std::string& key) const {
    for (const auto& item : obj) if (item.first == key) return &item.second;
    return nullptr;
  }
  std::optional<std::string> getString(const std::string& key) const {
    const Value* value = find(key);
    if (value && value->isString()) return value->str;
    return std::nullopt;
  }
  Value& set(std::string key, Value value) {
    for (auto& item : obj) if (item.first == key) { item.second = std::move(value); return *this; }
    obj.emplace_back(std::move(key), std::move(value)); return *this;
  }
  Value& push(Value value) { arr.push_back(std::move(value)); return *this; }
  std::string dump() const;
};
std::optional<Value> parse(const std::string& text);
std::string escape(const std::string& s);
}
