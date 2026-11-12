const test = require("node:test");
const assert = require("node:assert/strict");
const FriendGraph = require("../js/graph.js");

const ex = () => FriendGraph.example();

test("addPerson validates and de-duplicates (case-insensitive)", () => {
  const g = new FriendGraph();
  assert.equal(g.addPerson("  Asha  ").ok, true);
  assert.equal(g.addPerson("asha").ok, false);
  assert.equal(g.addPerson("   ").ok, false);
  assert.equal(g.addPerson("x".repeat(31)).ok, false);
  assert.deepEqual(g.people(), ["Asha"]);
});

test("friendships are symmetric, no self or duplicate links", () => {
  const g = new FriendGraph(); g.addPerson("A"); g.addPerson("B");
  assert.equal(g.addFriendship("A", "B").ok, true);
  assert.deepEqual(g.friendsOf("B"), ["A"]);
  assert.equal(g.addFriendship("B", "A").ok, false);
  assert.equal(g.addFriendship("A", "A").ok, false);
  assert.equal(g.addFriendship("A", "Z").ok, false);
});

test("common friends", () => {
  assert.deepEqual(ex().commonFriends("Asha", "Dev"), ["Ben"]);
  assert.deepEqual(ex().commonFriends("Asha", "Gopal"), []);
});

test("shortest route (BFS)", () => {
  const g = ex();
  assert.deepEqual(g.shortestPath("Asha", "Farah"), ["Asha", "Chitra", "Esha", "Farah"]);
  assert.deepEqual(g.shortestPath("Asha", "Asha"), ["Asha"]);
  assert.equal(g.shortestPath("Asha", "Gopal"), null);
  assert.equal(g.shortestPath("Asha", "Nobody"), null);
});

test("suggestions rank by mutual friends and exclude existing friends", () => {
  const s = ex().suggestions("Asha");
  assert.deepEqual(s.map(x => x.person), ["Dev", "Esha"]);
  assert.deepEqual(s[0].mutual, ["Ben"]);
});

test("BFS and DFS reach the same people in different order", () => {
  const g = ex();
  const bfs = g.traverse("Asha", "bfs"), dfs = g.traverse("Asha", "dfs");
  assert.deepEqual(bfs, ["Asha", "Ben", "Chitra", "Dev", "Esha", "Farah"]);
  assert.deepEqual(dfs, ["Asha", "Ben", "Chitra", "Esha", "Dev", "Farah"]);
  assert.deepEqual([...bfs].sort(), [...dfs].sort());
});

test("traversal never includes people outside the connected group", () => {
  assert.ok(!ex().traverse("Asha", "bfs").includes("Gopal"));
  assert.deepEqual(ex().traverse("Gopal", "dfs"), ["Gopal", "Hana"]);
});

test("removePerson cleans up links", () => {
  const g = ex(); g.removePerson("Ben");
  assert.ok(!g.people().includes("Ben"));
  assert.ok(!g.friendsOf("Asha").includes("Ben"));
});

test("JSON round trip", () => {
  const g = FriendGraph.fromJSON(JSON.parse(JSON.stringify(ex().toJSON())));
  assert.deepEqual(g.toJSON(), ex().toJSON());
});
