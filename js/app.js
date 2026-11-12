/* Friend Finder UI. All user text is inserted with textContent (never innerHTML).
 * Data comes from a store (C++ backend or in-browser fallback, see store.js);
 * `graph` below is only a local mirror used for drawing. */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  var NS = "http://www.w3.org/2000/svg";
  var THEME_KEY = "friend-finder:theme";

  var store = null;
  var graph = new FriendGraph();      // mirror of the current network, for drawing only
  var mode = "common";
  var hl = { path: [], visit: [], match: [], steps: {} };

  function load(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
  function save(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* storage unavailable */ } }

  /* ---------- helpers ---------- */
  function el(tag, attrs, text) {
    var n = document.createElement(tag);
    if (attrs) Object.keys(attrs).forEach(function (a) { n.setAttribute(a, attrs[a]); });
    if (text != null) n.textContent = text;
    return n;
  }
  function chips(names) {
    var f = document.createDocumentFragment();
    names.forEach(function (n) { f.appendChild(el("span", { "class": "chip" }, n)); });
    return f;
  }
  function say(node, text, isErr) { node.textContent = text; node.classList.toggle("err", !!isErr); }
  function clearHl() { hl = { path: [], visit: [], match: [], steps: {} }; }
  function setNetwork(net) { graph = FriendGraph.fromJSON(net); }
  function failText(r) { return (r && r.error) || "Something went wrong. Try again."; }
  function busy(btn, on) { btn.disabled = !!on; }

  /* ---------- selects ---------- */
  function fillSelect(sel, keepValue) {
    var prev = keepValue ? sel.value : "";
    sel.textContent = "";
    sel.appendChild(el("option", { value: "" }, "Choose a person"));
    graph.people().forEach(function (p) { sel.appendChild(el("option", { value: p }, p)); });
    if (prev && graph.has(prev)) sel.value = prev;
  }
  function refreshSelects() { ["fa", "fb", "ea", "eb"].forEach(function (id) { fillSelect($(id), true); }); }

  /* ---------- drawing ---------- */
  function layout(names) {
    var W = 640, H = 460, cx = W / 2, cy = H / 2, r = Math.min(W, H) / 2 - 50, pos = {};
    if (names.length === 1) { pos[names[0]] = { x: cx, y: cy }; return pos; }
    names.forEach(function (n, i) {
      var a = (2 * Math.PI * i) / names.length - Math.PI / 2;
      pos[n] = { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
    });
    return pos;
  }
  function render() {
    var names = graph.people();
    var empty = names.length === 0;
    $("empty").hidden = !empty || !store;
    $("canvas").toggleAttribute("hidden", empty); // SVG has no .hidden property
    var svg = $("canvas");
    svg.textContent = "";
    var pos = layout(names);
    var pathEdges = {};
    for (var i = 0; i < hl.path.length - 1; i++) {
      pathEdges[hl.path[i] + "\u0000" + hl.path[i + 1]] = pathEdges[hl.path[i + 1] + "\u0000" + hl.path[i]] = true;
    }
    graph.edges().forEach(function (e) {
      var a = pos[e[0]], b = pos[e[1]];
      var line = document.createElementNS(NS, "line");
      line.setAttribute("x1", a.x); line.setAttribute("y1", a.y);
      line.setAttribute("x2", b.x); line.setAttribute("y2", b.y);
      line.setAttribute("class", "edge" + (pathEdges[e[0] + "\u0000" + e[1]] ? " path" : ""));
      svg.appendChild(line);
    });
    var picks = [$("ea").value, $("eb").value, $("fa").value, $("fb").value];
    names.forEach(function (n) {
      var g = document.createElementNS(NS, "g");
      var cls = "node";
      if (hl.path.indexOf(n) > -1) cls += " path";
      else if (hl.match.indexOf(n) > -1) cls += " match";
      else if (hl.visit.indexOf(n) > -1) cls += " visit";
      if (picks.indexOf(n) > -1) cls += " pick";
      g.setAttribute("class", cls);
      g.setAttribute("transform", "translate(" + pos[n].x + "," + pos[n].y + ")");
      var c = document.createElementNS(NS, "circle"); c.setAttribute("r", 26);
      var t = document.createElementNS(NS, "text"); t.setAttribute("y", 5);
      t.textContent = n.length > 8 ? n.slice(0, 7) + "…" : n;
      var title = document.createElementNS(NS, "title"); title.textContent = n + " – click to choose";
      g.appendChild(title); g.appendChild(c); g.appendChild(t);
      if (hl.steps[n] != null) {
        var s = document.createElementNS(NS, "text");
        s.setAttribute("class", "step"); s.setAttribute("y", -30); s.textContent = hl.steps[n];
        s.style.fill = "currentColor";
        g.appendChild(s);
      }
      g.addEventListener("click", function () { choose(n); });
      svg.appendChild(g);
    });
    renderAdj();
  }
  function renderAdj() {
    var lines = graph.people().map(function (p) {
      var f = graph.friendsOf(p);
      return p + " -> " + (f.length ? f.join(", ") : "(no friends yet)");
    });
    $("adjlist").textContent = lines.join("\n") || "(empty)";
  }
  function choose(n) {
    var a = $("ea"), b = $("eb");
    if (!a.value || (a.value && b.value && mode !== "suggest" && mode !== "visit")) { a.value = n; b.value = ""; }
    else if (mode === "suggest" || mode === "visit") { a.value = n; }
    else if (a.value === n) { a.value = ""; }
    else { b.value = n; }
    $("fa").value = a.value; $("fb").value = b.value;
    render();
  }
  function updateHints() {
    say($("friendMsg"), graph.people().length < 2 ? "Add at least two people to begin." : "");
  }
  function applyNetwork(r) {      // after any mutation that returned a fresh network
    if (r && r.network) { setNetwork(r.network); refreshSelects(); updateHints(); }
  }

  /* ---------- actions ---------- */
  $("addForm").addEventListener("submit", function (e) {
    e.preventDefault();
    var btn = e.target.querySelector("button"); busy(btn, true);
    store.addPerson($("nameInput").value).then(function (r) {
      busy(btn, false);
      if (!r.ok) { say($("addMsg"), failText(r), true); return; }
      $("nameInput").value = ""; $("nameInput").focus();
      say($("addMsg"), r.name + " added.");
      clearHl(); applyNetwork(r); render();
    });
  });
  $("friendForm").addEventListener("submit", function (e) {
    e.preventDefault();
    var a = $("fa").value, b = $("fb").value, btn = e.target.querySelector("button"); busy(btn, true);
    store.addFriendship(a, b).then(function (r) {
      busy(btn, false);
      if (!r.ok) { say($("friendMsg"), failText(r), true); return; }
      say($("friendMsg"), a + " and " + b + " are now friends.");
      clearHl(); applyNetwork(r); render();
    });
  });
  function loadExample() {
    store.loadExample().then(function (r) {
      if (!r.ok) { say($("addMsg"), failText(r), true); return; }
      clearHl(); applyNetwork(r);
      $("ea").value = "Asha"; $("eb").value = "Farah"; $("fa").value = "Asha"; $("fb").value = "Dev";
      render();
    });
  }
  $("example").addEventListener("click", loadExample);
  $("example2").addEventListener("click", loadExample);
  $("reset").addEventListener("click", function () {
    if (graph.people().length && !window.confirm("Remove everyone and start over?")) return;
    store.reset().then(function (r) {
      if (!r.ok) { say($("addMsg"), failText(r), true); return; }
      clearHl(); $("result").textContent = "Pick people above, then press the button.";
      applyNetwork(r); render();
    });
  });

  /* ---------- explore ---------- */
  var LABEL = { common: "Find friends in common", route: "Find the shortest route", suggest: "Suggest new friends", visit: "Visit everyone" };
  Array.prototype.forEach.call(document.querySelectorAll(".tabs [role=tab]"), function (t) {
    t.addEventListener("click", function () {
      mode = t.getAttribute("data-mode");
      Array.prototype.forEach.call(document.querySelectorAll(".tabs [role=tab]"), function (x) {
        x.setAttribute("aria-selected", x === t ? "true" : "false");
      });
      $("run").textContent = LABEL[mode];
      var two = mode === "common" || mode === "route";
      $("eb").hidden = !two; $("eand").hidden = !two; $("order").hidden = mode !== "visit";
      clearHl(); $("result").textContent = "Pick people above, then press the button."; render();
    });
  });
  ["ea", "eb", "fa", "fb"].forEach(function (id) { $(id).addEventListener("change", render); });

  function showResult(a, b, r) {
    var out = $("result"); out.textContent = "";
    if (!r.ok) { out.textContent = failText(r); render(); return; }
    if (mode === "common") {
      hl.match = r.common;
      if (!r.common.length) out.textContent = a + " and " + b + " have no friends in common.";
      else { out.appendChild(el("div", null, a + " and " + b + " both know:")); out.appendChild(chips(r.common)); }
    } else if (mode === "route") {
      if (!r.path) out.textContent = "There is no route between " + a + " and " + b + " yet. Try making a friendship that links their groups.";
      else {
        hl.path = r.path;
        var hops = r.path.length - 1;
        out.appendChild(el("div", null, hops === 1 ? a + " and " + b + " are already friends." : hops + " steps apart:"));
        var ol = el("ol");
        r.path.forEach(function (n) { ol.appendChild(el("li", null, n)); });
        out.appendChild(ol);
      }
    } else if (mode === "suggest") {
      var s = r.suggestions;
      if (!s.length) out.textContent = "No suggestions for " + a + " yet. They need friends who have other friends.";
      else {
        hl.match = s.map(function (x) { return x.person; });
        out.appendChild(el("div", null, "People " + a + " might know:"));
        var ul = el("ul");
        s.forEach(function (x) {
          ul.appendChild(el("li", null, x.person + " – " + x.mutual.length + " friend" + (x.mutual.length > 1 ? "s" : "") + " in common (" + x.mutual.join(", ") + ")"));
        });
        out.appendChild(ul);
      }
    } else {
      var kind = $("order").value, order = r.order;
      hl.visit = order;
      order.forEach(function (n, i) { hl.steps[n] = i + 1; });
      out.appendChild(el("div", null, (kind === "bfs" ? "Nearest first" : "Deep first") + " from " + a + ", " + order.length + " " + (order.length === 1 ? "person" : "people") + " reached:"));
      var ol2 = el("ol"); order.forEach(function (n) { ol2.appendChild(el("li", null, n)); }); out.appendChild(ol2);
      if (order.length < r.total) out.appendChild(el("div", { "class": "small" }, (r.total - order.length) + " not connected to " + a + "."));
    }
    render();
  }

  $("run").addEventListener("click", function () {
    var a = $("ea").value, b = $("eb").value, out = $("result"), btn = $("run");
    clearHl(); out.textContent = "";
    var two = mode === "common" || mode === "route";
    if (!a || (two && !b)) { out.textContent = two ? "Choose two people first." : "Choose a person first."; render(); return; }
    if (two && a === b) { out.textContent = "Choose two different people."; render(); return; }
    var p;
    if (mode === "common") p = store.common(a, b);
    else if (mode === "route") p = store.route(a, b);
    else if (mode === "suggest") p = store.suggestions(a);
    else p = store.traverse(a, $("order").value);
    busy(btn, true);
    p.then(function (r) { busy(btn, false); showResult(a, b, r); });
  });

  /* ---------- theme ---------- */
  var themeSel = $("theme");
  var savedTheme = load(THEME_KEY);
  if (savedTheme && /^(warm|night|sea)$/.test(savedTheme)) { themeSel.value = savedTheme; document.body.setAttribute("data-theme", savedTheme); }
  themeSel.addEventListener("change", function () {
    document.body.setAttribute("data-theme", themeSel.value); save(THEME_KEY, themeSel.value);
  });

  /* ---------- boot ---------- */
  createStore().then(function (s) {
    store = s;
    var m = $("mode");
    if (s.kind === "server") { m.textContent = "Connected to the C++ server. Your network is saved there."; m.className = "mode server"; }
    else { m.textContent = "No server found, so everything runs in your browser and is saved on this device."; m.className = "mode local"; }
    return s.init();
  }).then(function (net) {
    setNetwork(net); refreshSelects(); updateHints(); render();
  });
})();
