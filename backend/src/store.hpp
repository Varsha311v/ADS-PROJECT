// Thread-safe, file-persisted wrapper around Graph.
#pragma once
#include <mutex>
#include <string>

#include "graph.hpp"
#include "json.hpp"

namespace ff {

class Store {
 public:
  explicit Store(std::string dataFile) : path_(std::move(dataFile)) { load(); }

  // Run fn(graph) under the lock. If it returns true the graph is saved to disk.
  template <class F>
  auto with(F&& fn) {
    std::lock_guard<std::mutex> lk(m_);
    return fn(g_);
  }
  void save();  // call while holding the lock (inside with())

  static json::Value networkJson(const Graph& g);

 private:
  void load();
  std::mutex m_;
  Graph g_;
  std::string path_;
};

}  // namespace ff
