// Friend graph: adjacency list + BFS/DFS/set-intersection features.
// Same behaviour as js/graph.js. Not thread-safe by itself; the server locks around it.
#pragma once
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ff {

struct Result {
  bool ok = true;
  std::string error;  // set when ok == false
  std::string name;   // normalised name for addPerson
  bool conflict = false;  // true for duplicates (maps to HTTP 409)
};

struct Suggestion {
  std::string person;
  std::vector<std::string> mutual;
};

class Graph {
 public:
  static constexpr size_t kMaxNameChars = 30;
  static constexpr size_t kMaxPeople = 200;
  static constexpr size_t kMaxFriendships = 5000;

  // Trim, collapse inner whitespace runs to one space.
  static std::string clean(const std::string& raw);

  Result addPerson(const std::string& raw);
  bool removePerson(const std::string& name);
  Result addFriendship(const std::string& a, const std::string& b);
  bool removeFriendship(const std::string& a, const std::string& b);
  void clear();

  bool has(const std::string& name) const { return adj_.count(name) > 0; }
  const std::vector<std::string>& people() const { return order_; }  // insertion order
  std::vector<std::string> friendsOf(const std::string& name) const;  // sorted
  std::vector<std::pair<std::string, std::string>> edges() const;     // a < b, sorted

  std::vector<std::string> commonFriends(const std::string& a, const std::string& b) const;
  std::optional<std::vector<std::string>> shortestPath(const std::string& from,
                                                       const std::string& to) const;
  std::vector<Suggestion> suggestions(const std::string& name) const;
  std::vector<std::string> traverse(const std::string& start, bool dfs) const;

  static Graph example();

 private:
  std::vector<std::string> order_;
  std::unordered_map<std::string, std::set<std::string>> adj_;
  size_t edgeCount_ = 0;
};

}  // namespace ff
