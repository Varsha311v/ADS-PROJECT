/* Data layer. Two interchangeable stores with the same async interface:
 *   ApiStore   – talks to the C++ backend (REST, state lives on the server)
 *   LocalStore – runs FriendGraph in the browser (state in localStorage)
 * createStore() picks the C++ backend when it answers /api/health, otherwise the local store. */
(function (root) {
  "use strict";
  var KEY = "friend-finder:v1";

  /* ---------- local (in-browser) ---------- */
  function LocalStore() {
    this.kind = "local";
    this.g = new FriendGraph();
  }
  LocalStore.prototype._save = function () { try { localStorage.setItem(KEY, JSON.stringify(this.g.toJSON())); } catch (e) { /* storage unavailable */ } };
  LocalStore.prototype.init = function () {
    try { var raw = localStorage.getItem(KEY); if (raw) this.g = FriendGraph.fromJSON(JSON.parse(raw)); } catch (e) { this.g = new FriendGraph(); }
    return Promise.resolve(this.g.toJSON());
  };
  LocalStore.prototype.addPerson = function (name) {
    var r = this.g.addPerson(name); if (r.ok) { this._save(); r.network = this.g.toJSON(); } return Promise.resolve(r);
  };
  LocalStore.prototype.addFriendship = function (a, b) {
    var r = this.g.addFriendship(a, b); if (r.ok) { this._save(); r.network = this.g.toJSON(); } return Promise.resolve(r);
  };
  LocalStore.prototype.loadExample = function () { this.g = FriendGraph.example(); this._save(); return Promise.resolve({ ok: true, network: this.g.toJSON() }); };
  LocalStore.prototype.reset = function () { this.g = new FriendGraph(); this._save(); return Promise.resolve({ ok: true, network: this.g.toJSON() }); };
  LocalStore.prototype.common = function (a, b) { return Promise.resolve({ ok: true, common: this.g.commonFriends(a, b) }); };
  LocalStore.prototype.route = function (a, b) { return Promise.resolve({ ok: true, path: this.g.shortestPath(a, b) }); };
  LocalStore.prototype.suggestions = function (n) { return Promise.resolve({ ok: true, suggestions: this.g.suggestions(n) }); };
  LocalStore.prototype.traverse = function (n, mode) {
    return Promise.resolve({ ok: true, order: this.g.traverse(n, mode), total: this.g.people().length });
  };

  /* ---------- C++ backend ---------- */
  function ApiStore(base) { this.kind = "server"; this.base = base; }
  ApiStore.prototype._call = function (method, path, body) {
    var opts = { method: method };
    if (body !== undefined) { opts.headers = { "Content-Type": "application/json" }; opts.body = JSON.stringify(body); }
    return fetch(this.base + path, opts).then(function (res) {
      return res.json().then(function (js) { return js; }, function () {
        return { ok: false, error: "Unexpected reply from the server (HTTP " + res.status + ")." };
      });
    }, function () {
      return { ok: false, offline: true, error: "Can't reach the server. Check that it is running." };
    });
  };
  var q = encodeURIComponent;
  ApiStore.prototype.init = function () {
    return this._call("GET", "/api/network").then(function (r) { return r.ok ? r.network : { people: [], friendships: [] }; });
  };
  ApiStore.prototype.addPerson = function (name) { return this._call("POST", "/api/people", { name: name }); };
  ApiStore.prototype.addFriendship = function (a, b) { return this._call("POST", "/api/friendships", { a: a, b: b }); };
  ApiStore.prototype.loadExample = function () { return this._call("POST", "/api/network/example"); };
  ApiStore.prototype.reset = function () { return this._call("POST", "/api/network/reset"); };
  ApiStore.prototype.common = function (a, b) { return this._call("GET", "/api/common?a=" + q(a) + "&b=" + q(b)); };
  ApiStore.prototype.route = function (a, b) { return this._call("GET", "/api/route?from=" + q(a) + "&to=" + q(b)); };
  ApiStore.prototype.suggestions = function (n) { return this._call("GET", "/api/suggestions?name=" + q(n)); };
  ApiStore.prototype.traverse = function (n, mode) { return this._call("GET", "/api/traverse?start=" + q(n) + "&mode=" + q(mode)); };

  /* ---------- pick one ---------- */
  function detectBackend(base) {
    var ctl = typeof AbortController === "function" ? new AbortController() : null;
    var timer = setTimeout(function () { if (ctl) ctl.abort(); }, 2500);
    return fetch(base + "/api/health", ctl ? { signal: ctl.signal } : {}).then(function (res) {
      return res.ok ? res.json() : null;
    }).then(function (js) { clearTimeout(timer); return !!(js && js.status === "ok"); }, function () { clearTimeout(timer); return false; });
  }
  root.createStore = function () {
    var base = String(root.FRIEND_FINDER_API || "").replace(/\/+$/, "");
    // Opened straight from disk with no API URL configured: there is nothing to probe.
    if (!base && root.location && root.location.protocol === "file:") return Promise.resolve(new LocalStore());
    return detectBackend(base).then(function (up) { return up ? new ApiStore(base) : new LocalStore(); });
  };
})(window);
