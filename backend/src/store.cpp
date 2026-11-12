#include "store.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ff {

json::Value Store::networkJson(const Graph& g) {
  json::Value people = json::Value::array();
  for (const auto& p : g.people()) people.push(json::Value::string(p));
  json::Value edges = json::Value::array();
  for (const auto& [a, b] : g.edges()) {
    json::Value e = json::Value::array();
    e.push(json::Value::string(a)).push(json::Value::string(b));
    edges.push(std::move(e));
  }
  json::Value net = json::Value::object();
  net.set("people", std::move(people)).set("friendships", std::move(edges));
  return net;
}

void Store::load() {
  if (path_.empty()) return;
  std::ifstream in(path_, std::ios::binary);
  if (!in) return;  // first run: no file yet
  std::stringstream ss; ss << in.rdbuf();
  auto doc = json::parse(ss.str());
  if (!doc || !doc->isObject()) { std::fprintf(stderr, "warning: could not parse %s, starting empty\n", path_.c_str()); return; }
  if (const auto* ps = doc->find("people"); ps && ps->isArray())
    for (const auto& p : ps->arr) if (p.isString()) g_.addPerson(p.str);
  if (const auto* es = doc->find("friendships"); es && es->isArray())
    for (const auto& e : es->arr)
      if (e.isArray() && e.arr.size() == 2 && e.arr[0].isString() && e.arr[1].isString())
        g_.addFriendship(e.arr[0].str, e.arr[1].str);
  std::fprintf(stderr, "loaded %zu people from %s\n", g_.people().size(), path_.c_str());
}

void Store::save() {
  if (path_.empty()) return;
  namespace fs = std::filesystem;
  try {
    fs::path p(path_);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::string tmp = path_ + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) throw std::runtime_error("cannot open " + tmp);
      out << networkJson(g_).dump();
      if (!out) throw std::runtime_error("write failed");
    }
    fs::rename(tmp, p);  // atomic replace on POSIX
  } catch (const std::exception& e) {
    std::fprintf(stderr, "warning: could not save data: %s\n", e.what());
  }
}

}  // namespace ff
