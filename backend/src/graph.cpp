#include "graph.hpp"

#include <algorithm>
#include <cctype>
#include <deque>

namespace ff {
namespace {

bool isSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

std::string lower(std::string s) {  // ASCII-only case folding
  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

// Number of Unicode code points in a UTF-8 string (counts non-continuation bytes).
size_t codepoints(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

bool validUtf8(const std::string& s) {
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = s[i];
    size_t len; unsigned cp;
    if (c < 0x80) { ++i; continue; }
    else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return false;
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k) {
      unsigned char cc = s[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;  // overlong
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len;
  }
  return true;
}

}  // namespace

std::string Graph::clean(const std::string& raw) {
  std::string out;
  bool pendingSpace = false;
  for (unsigned char c : raw) {
    if (isSpace(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(static_cast<char>(c));
  }
  return out;
}

Result Graph::addPerson(const std::string& raw) {
  Result r;
  std::string name = clean(raw);
  if (name.empty()) return {false, "Type a name first.", ""};
  if (codepoints(name) > kMaxNameChars) return {false, "Names can be up to 30 characters.", ""};
  if (!validUtf8(name)) return {false, "Names must be valid UTF-8 text.", ""};
  for (unsigned char c : name)
    if (c < 0x20 || c == 0x7F) return {false, "Names cannot contain control characters.", ""};
  if (order_.size() >= kMaxPeople) return {false, "The network is full (200 people).", ""};
  const std::string lname = lower(name);
  for (const auto& p : order_)
    if (lower(p) == lname) return {false, p + " is already in the network.", "", true};
  adj_[name];
  order_.push_back(name);
  r.name = name;
  return r;
}

bool Graph::removePerson(const std::string& name) {
  auto it = adj_.find(name);
  if (it == adj_.end()) return false;
  for (const auto& f : it->second) {
    adj_[f].erase(name);
    --edgeCount_;
  }
  adj_.erase(it);
  order_.erase(std::remove(order_.begin(), order_.end(), name), order_.end());
  return true;
}

Result Graph::addFriendship(const std::string& a, const std::string& b) {
  if (!has(a) || !has(b)) return {false, "Pick two people from the list.", ""};
  if (a == b) return {false, "Pick two different people.", ""};
  if (adj_[a].count(b)) return {false, a + " and " + b + " are already friends.", "", true};
  if (edgeCount_ >= kMaxFriendships) return {false, "Too many friendships.", ""};
  adj_[a].insert(b);
  adj_[b].insert(a);
  ++edgeCount_;
  return {};
}

bool Graph::removeFriendship(const std::string& a, const std::string& b) {
  if (!has(a) || !has(b) || !adj_[a].count(b)) return false;
  adj_[a].erase(b);
  adj_[b].erase(a);
  --edgeCount_;
  return true;
}

void Graph::clear() {
  order_.clear();
  adj_.clear();
  edgeCount_ = 0;
}

std::vector<std::string> Graph::friendsOf(const std::string& name) const {
  auto it = adj_.find(name);
  if (it == adj_.end()) return {};
  return {it->second.begin(), it->second.end()};
}

std::vector<std::pair<std::string, std::string>> Graph::edges() const {
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto& [a, set] : adj_)
    for (const auto& b : set)
      if (a < b) out.emplace_back(a, b);
  std::sort(out.begin(), out.end());
  return out;
}

// Set intersection of two friend lists. O(d1 + d2) with the smaller list scanned.
std::vector<std::string> Graph::commonFriends(const std::string& a, const std::string& b) const {
  auto ia = adj_.find(a), ib = adj_.find(b);
  if (ia == adj_.end() || ib == adj_.end()) return {};
  const auto& small = ia->second.size() <= ib->second.size() ? ia->second : ib->second;
  const auto& big = &small == &ia->second ? ib->second : ia->second;
  std::vector<std::string> out;
  for (const auto& x : small)
    if (big.count(x) && x != a && x != b) out.push_back(x);
  return out;  // already sorted (std::set order)
}

// BFS shortest route. O(V + E).
std::optional<std::vector<std::string>> Graph::shortestPath(const std::string& from,
                                                            const std::string& to) const {
  if (!has(from) || !has(to)) return std::nullopt;
  if (from == to) return std::vector<std::string>{from};
  std::unordered_map<std::string, std::string> prev;
  std::unordered_map<std::string, bool> seen;
  std::deque<std::string> q;
  seen[from] = true;
  q.push_back(from);
  while (!q.empty()) {
    std::string cur = std::move(q.front());
    q.pop_front();
    for (const auto& n : adj_.at(cur)) {
      if (seen[n]) continue;
      seen[n] = true;
      prev[n] = cur;
      if (n == to) {
        std::vector<std::string> path;
        for (std::string x = to;; x = prev[x]) {
          path.push_back(x);
          if (x == from) break;
        }
        std::reverse(path.begin(), path.end());
        return path;
      }
      q.push_back(n);
    }
  }
  return std::nullopt;
}

// Friends of friends who are not friends yet, ranked by number of mutual friends. O(d^2).
std::vector<Suggestion> Graph::suggestions(const std::string& name) const {
  auto it = adj_.find(name);
  if (it == adj_.end()) return {};
  const auto& mine = it->second;
  std::unordered_map<std::string, std::vector<std::string>> score;
  for (const auto& f : mine)
    for (const auto& ff : adj_.at(f)) {
      if (ff == name || mine.count(ff)) continue;
      score[ff].push_back(f);
    }
  std::vector<Suggestion> out;
  out.reserve(score.size());
  for (auto& [person, via] : score) {
    std::sort(via.begin(), via.end());
    out.push_back({person, std::move(via)});
  }
  std::sort(out.begin(), out.end(), [](const Suggestion& x, const Suggestion& y) {
    if (x.mutual.size() != y.mutual.size()) return x.mutual.size() > y.mutual.size();
    return x.person < y.person;
  });
  return out;
}

// Visit order of start's connected group. BFS (queue) or DFS (stack). O(V + E).
std::vector<std::string> Graph::traverse(const std::string& start, bool dfs) const {
  if (!has(start)) return {};
  std::vector<std::string> order;
  if (dfs) {
    std::vector<std::string> stack{start};
    std::unordered_map<std::string, bool> done;
    while (!stack.empty()) {
      std::string u = std::move(stack.back());
      stack.pop_back();
      if (done[u]) continue;
      done[u] = true;
      order.push_back(u);
      const auto& fr = adj_.at(u);
      for (auto it = fr.rbegin(); it != fr.rend(); ++it)
        if (!done[*it]) stack.push_back(*it);
    }
    return order;
  }
  std::unordered_map<std::string, bool> seen;
  std::deque<std::string> q{start};
  seen[start] = true;
  while (!q.empty()) {
    std::string u = std::move(q.front());
    q.pop_front();
    order.push_back(u);
    for (const auto& n : adj_.at(u))
      if (!seen[n]) {
        seen[n] = true;
        q.push_back(n);
      }
  }
  return order;
}

Graph Graph::example() {
  Graph g;
  for (const char* p : {"Asha", "Ben", "Chitra", "Dev", "Esha", "Farah", "Gopal", "Hana"})
    g.addPerson(p);
  const char* e[][2] = {{"Asha", "Ben"},   {"Asha", "Chitra"}, {"Ben", "Chitra"}, {"Ben", "Dev"},
                        {"Chitra", "Esha"}, {"Dev", "Esha"},    {"Esha", "Farah"}, {"Gopal", "Hana"}};
  for (auto& pair : e) g.addFriendship(pair[0], pair[1]);
  return g;
}

}  // namespace ff
