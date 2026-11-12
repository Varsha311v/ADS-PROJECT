// Dependency-free unit tests. Build & run: make test
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../src/graph.hpp"
#include "../src/http.hpp"
#include "../src/json.hpp"

using ff::Graph;
using V = std::vector<std::string>;

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_fail; std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(); static Reg r_##name(#name, name); static void name()
struct Reg { Reg(const char* n, void (*f)()); };
static std::vector<std::pair<const char*, void (*)()>>& tests() { static std::vector<std::pair<const char*, void (*)()>> t; return t; }
Reg::Reg(const char* n, void (*f)()) { tests().push_back({n, f}); }

TEST(addPerson_validates_and_dedupes) {
  Graph g;
  CHECK(g.addPerson("  Asha   Rao ").ok);
  CHECK(g.people() == V{"Asha Rao"});
  auto dup = g.addPerson("asha rao"); CHECK(!dup.ok && dup.conflict);
  CHECK(!g.addPerson("   ").ok);
  CHECK(!g.addPerson(std::string(31, 'x')).ok);
  CHECK(g.addPerson(std::string(30, 'x')).ok);
  CHECK(!g.addPerson("bad\x01name").ok);
  CHECK(!g.addPerson("\xff\xfe").ok);                 // invalid UTF-8
  CHECK(!g.addPerson("\xC0\xAF").ok);                 // overlong encoding
  CHECK(g.addPerson("Zo\xC3\xAB").ok);                // valid 2-byte char
  CHECK(!g.addPerson(std::string(30, '\xC3')).ok);  // lone lead bytes are invalid UTF-8
}

TEST(unicode_name_length_counts_codepoints) {
  Graph g;
  std::string thirty; for (int i = 0; i < 30; ++i) thirty += "\xC3\xA9";  // 30 x "é" = 60 bytes
  CHECK(g.addPerson(thirty).ok);
  std::string thirtyOne = thirty + "\xC3\xA9";
  CHECK(!g.addPerson(thirtyOne).ok);
}

TEST(friendships_symmetric_no_self_or_dupes) {
  Graph g; g.addPerson("A"); g.addPerson("B");
  CHECK(g.addFriendship("A", "B").ok);
  CHECK(g.friendsOf("B") == V{"A"});
  auto dup = g.addFriendship("B", "A"); CHECK(!dup.ok && dup.conflict);
  CHECK(!g.addFriendship("A", "A").ok);
  CHECK(!g.addFriendship("A", "Z").ok);
  CHECK(g.removeFriendship("A", "B"));
  CHECK(!g.removeFriendship("A", "B"));
}

TEST(common_friends) {
  Graph g = Graph::example();
  CHECK(g.commonFriends("Asha", "Dev") == V{"Ben"});
  CHECK(g.commonFriends("Asha", "Gopal").empty());
  CHECK(g.commonFriends("Asha", "Nobody").empty());
}

TEST(shortest_route_bfs) {
  Graph g = Graph::example();
  auto p = g.shortestPath("Asha", "Farah");
  CHECK(p && *p == (V{"Asha", "Chitra", "Esha", "Farah"}));
  auto self = g.shortestPath("Asha", "Asha"); CHECK(self && *self == V{"Asha"});
  CHECK(!g.shortestPath("Asha", "Gopal"));
  CHECK(!g.shortestPath("Asha", "Nobody"));
}

TEST(suggestions_ranked_by_mutual_friends) {
  Graph g = Graph::example();
  auto s = g.suggestions("Asha");
  CHECK(s.size() == 2 && s[0].person == "Dev" && s[1].person == "Esha");
  CHECK(s[0].mutual == V{"Ben"});
  // add a 2nd mutual for Esha via Ben so she outranks Dev
  g.addFriendship("Ben", "Esha");
  s = g.suggestions("Asha");
  CHECK(s[0].person == "Esha" && s[0].mutual.size() == 2);
}

TEST(bfs_dfs_order_and_group_only) {
  Graph g = Graph::example();
  CHECK(g.traverse("Asha", false) == (V{"Asha", "Ben", "Chitra", "Dev", "Esha", "Farah"}));
  CHECK(g.traverse("Asha", true) == (V{"Asha", "Ben", "Chitra", "Esha", "Dev", "Farah"}));
  CHECK(g.traverse("Gopal", true) == (V{"Gopal", "Hana"}));
  CHECK(g.traverse("Nobody", false).empty());
}

TEST(remove_person_cleans_links_and_order) {
  Graph g = Graph::example();
  CHECK(g.removePerson("Ben"));
  CHECK(!g.has("Ben"));
  for (const auto& f : g.friendsOf("Asha")) CHECK(f != "Ben");
  for (const auto& p : g.people()) CHECK(p != "Ben");
  CHECK(!g.removePerson("Ben"));
  // edge count bookkeeping: after removals, adding the same edges again must still work
  CHECK(g.addFriendship("Asha", "Dev").ok);
}

TEST(limits_enforced) {
  Graph g;
  for (size_t i = 0; i < Graph::kMaxPeople; ++i) CHECK(g.addPerson("p" + std::to_string(i)).ok);
  CHECK(!g.addPerson("overflow").ok);
}

TEST(insertion_order_preserved) {
  Graph g; g.addPerson("Zed"); g.addPerson("Amy"); g.addPerson("Moe");
  CHECK(g.people() == (V{"Zed", "Amy", "Moe"}));
}

TEST(json_parse_valid) {
  auto v = ff::json::parse(R"({"a":[1,2.5,true,null,"x\n\u00e9\ud83d\ude00"],"b":{"c":"d"}})");
  CHECK(v && v->isObject());
  CHECK(v->find("a")->arr.size() == 5);
  CHECK(v->find("a")->arr[4].str == "x\n\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(v->find("b")->getString("c") == std::string("d"));
}

TEST(json_parse_rejects_garbage) {
  for (const char* bad : {"", "{", "{\"a\"}", "{\"a\":}", "[1,]", "{\"a\":1,}", "\"unterminated", "nul", "{\"a\":\"\\x\"}",
                          "{\"a\":\"\\ud800\"}", "{\"a\":\"\\udc00\"}", "{} trailing", "{\"a\":\"line\nbreak\"}", "01x", "-"}) {
    CHECK(!ff::json::parse(bad));
  }
  std::string deep(100, '['); deep += std::string(100, ']');
  CHECK(!ff::json::parse(deep));  // depth limit
}

TEST(json_roundtrip_and_escape) {
  ff::json::Value o = ff::json::Value::object();
  o.set("s", ff::json::Value::string("q\"\\\n\t\x01 \xC3\xA9"));
  std::string out = o.dump();
  CHECK(out == "{\"s\":\"q\\\"\\\\\\n\\t\\u0001 \xC3\xA9\"}");
  auto back = ff::json::parse(out);
  CHECK(back && back->getString("s") == std::string("q\"\\\n\t\x01 \xC3\xA9"));
}

TEST(percent_decode) {
  std::string out;
  CHECK(ff::http::percentDecode("a%20b+c%C3%A9", out, true) && out == "a b c\xC3\xA9");
  CHECK(ff::http::percentDecode("a+b", out, false) && out == "a+b");
  CHECK(!ff::http::percentDecode("%", out, false));
  CHECK(!ff::http::percentDecode("%4", out, false));
  CHECK(!ff::http::percentDecode("%zz", out, false));
  CHECK(ff::http::percentDecode("%41", out, false) && out == "A");
}

int main() {
  for (auto& [name, fn] : tests()) {
    int before = g_fail; ++g_run; fn();
    std::printf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
  }
  std::printf("\n%d tests, %d failed assertions\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
