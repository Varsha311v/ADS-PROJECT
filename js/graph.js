/* Pure graph logic for Friend Finder. No DOM access, so it can be unit tested in Node. */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.FriendGraph = factory();
})(typeof self !== "undefined" ? self : this, function () {
  class FriendGraph {
    constructor() {
      this.adj = new Map(); // name -> Set(names)  (adjacency list)
    }
    static clean(name) {
      return String(name == null ? "" : name).trim().replace(/\s+/g, " ");
    }
    has(name) { return this.adj.has(name); }
    people() { return [...this.adj.keys()]; }
    friendsOf(name) { return this.adj.has(name) ? [...this.adj.get(name)].sort() : []; }
    edges() {
      const out = [];
      for (const [a, set] of this.adj) for (const b of set) if (a < b) out.push([a, b]);
      return out;
    }
    addPerson(raw) {
      const name = FriendGraph.clean(raw);
      if (!name) return { ok: false, error: "Type a name first." };
      if (name.length > 30) return { ok: false, error: "Names can be up to 30 characters." };
      const dup = this.people().find(p => p.toLowerCase() === name.toLowerCase());
      if (dup) return { ok: false, error: dup + " is already in the network." };
      this.adj.set(name, new Set());
      return { ok: true, name };
    }
    removePerson(name) {
      if (!this.adj.has(name)) return false;
      for (const f of this.adj.get(name)) this.adj.get(f).delete(name);
      this.adj.delete(name);
      return true;
    }
    addFriendship(a, b) {
      if (!this.adj.has(a) || !this.adj.has(b)) return { ok: false, error: "Pick two people from the list." };
      if (a === b) return { ok: false, error: "Pick two different people." };
      if (this.adj.get(a).has(b)) return { ok: false, error: a + " and " + b + " are already friends." };
      this.adj.get(a).add(b);
      this.adj.get(b).add(a);
      return { ok: true };
    }
    removeFriendship(a, b) {
      if (!this.adj.has(a) || !this.adj.has(b)) return false;
      this.adj.get(a).delete(b);
      this.adj.get(b).delete(a);
      return true;
    }
    /** Set intersection of two friend lists. O(d1 + d2) */
    commonFriends(a, b) {
      if (!this.adj.has(a) || !this.adj.has(b)) return [];
      const [small, big] = this.adj.get(a).size <= this.adj.get(b).size ? [a, b] : [b, a];
      return [...this.adj.get(small)].filter(x => this.adj.get(big).has(x) && x !== a && x !== b).sort();
    }
    /** BFS shortest route. Returns array of names or null if not connected. O(V + E) */
    shortestPath(from, to) {
      if (!this.adj.has(from) || !this.adj.has(to)) return null;
      if (from === to) return [from];
      const prev = new Map([[from, null]]);
      const queue = [from];
      for (let i = 0; i < queue.length; i++) {
        const cur = queue[i];
        for (const n of this.friendsOf(cur)) {
          if (prev.has(n)) continue;
          prev.set(n, cur);
          if (n === to) {
            const path = [];
            for (let x = to; x !== null; x = prev.get(x)) path.push(x);
            return path.reverse();
          }
          queue.push(n);
        }
      }
      return null;
    }
    /** Friends of friends who are not yet friends, ranked by mutual friend count. O(d^2) */
    suggestions(name) {
      if (!this.adj.has(name)) return [];
      const mine = this.adj.get(name);
      const score = new Map();
      for (const f of mine) {
        for (const ff of this.adj.get(f)) {
          if (ff === name || mine.has(ff)) continue;
          if (!score.has(ff)) score.set(ff, []);
          score.get(ff).push(f);
        }
      }
      return [...score.entries()]
        .map(([person, via]) => ({ person, mutual: via.sort() }))
        .sort((x, y) => y.mutual.length - x.mutual.length || x.person.localeCompare(y.person));
    }
    /** Visit order from a start person (only their connected group). BFS or DFS. O(V + E) */
    traverse(start, mode) {
      if (!this.adj.has(start)) return [];
      const seen = new Set([start]);
      const order = [];
      if (mode === "dfs") {
        const stack = [start];
        const done = new Set();
        while (stack.length) {
          const u = stack.pop();
          if (done.has(u)) continue;
          done.add(u);
          order.push(u);
          const fr = this.friendsOf(u);
          for (let i = fr.length - 1; i >= 0; i--) if (!done.has(fr[i])) stack.push(fr[i]);
        }
        return order;
      }
      const queue = [start];
      for (let i = 0; i < queue.length; i++) {
        order.push(queue[i]);
        for (const n of this.friendsOf(queue[i])) if (!seen.has(n)) { seen.add(n); queue.push(n); }
      }
      return order;
    }
    toJSON() { return { people: this.people(), friendships: this.edges() }; }
    static fromJSON(data) {
      const g = new FriendGraph();
      if (!data || !Array.isArray(data.people)) return g;
      data.people.forEach(p => g.addPerson(p));
      (data.friendships || []).forEach(e => Array.isArray(e) && g.addFriendship(e[0], e[1]));
      return g;
    }
    static example() {
      return FriendGraph.fromJSON({
        people: ["Asha", "Ben", "Chitra", "Dev", "Esha", "Farah", "Gopal", "Hana"],
        friendships: [
          ["Asha", "Ben"], ["Asha", "Chitra"], ["Ben", "Chitra"], ["Ben", "Dev"],
          ["Chitra", "Esha"], ["Dev", "Esha"], ["Esha", "Farah"], ["Gopal", "Hana"]
        ]
      });
    }
  }
  return FriendGraph;
});
