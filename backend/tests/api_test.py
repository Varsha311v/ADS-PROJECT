#!/usr/bin/env python3
"""End-to-end API tests: starts the compiled server, talks real HTTP, restarts it to check persistence.
Usage: python3 tests/api_test.py   (run `make` first)"""
import http.client, json, os, shutil, signal, socket, subprocess, sys, tempfile, threading, time
from urllib.parse import quote

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.environ.get("FF_BIN") or os.path.join(HERE, "..", "build", "friend-finder")
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
passed = failed = 0

def check(name, cond, detail=""):
    global passed, failed
    if cond: passed += 1; print("PASS", name)
    else: failed += 1; print("FAIL", name, detail)

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

class Server:
    def __init__(self, data, port, extra=()):
        self.port = port
        self.log = open(LOG, "ab")
        self.p = subprocess.Popen([BIN, "--port", str(port), "--bind", "127.0.0.1", "--static", ROOT, "--data", data, *extra],
                                  stderr=self.log, stdout=subprocess.DEVNULL)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close(); return
            except OSError: time.sleep(0.05)
        raise RuntimeError("server did not start")
    def stop(self):
        self.p.send_signal(signal.SIGTERM)
        try: self.p.wait(5)
        except subprocess.TimeoutExpired: self.p.kill(); return None
        return self.p.returncode

def call(port, method, path, body=None, headers=None, raw_body=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    h = dict(headers or {})
    data = raw_body if raw_body is not None else (json.dumps(body) if body is not None else None)
    if data is not None and "Content-Type" not in h: h["Content-Type"] = "application/json"
    c.request(method, path, body=data, headers=h)
    r = c.getresponse(); txt = r.read(); hd = dict((k.lower(), v) for k, v in r.getheaders()); c.close()
    try: js = json.loads(txt)
    except Exception: js = None
    return r.status, js, hd, txt

def raw(port, payload: bytes, half_close=False, timeout=5):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout); s.sendall(payload)
    if half_close: s.shutdown(socket.SHUT_WR)
    out = b""
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            out += d
    except socket.timeout: pass
    s.close(); return out

tmp = tempfile.mkdtemp()
LOG = os.path.join(tmp, "server.log")
data = os.path.join(tmp, "sub", "net.json")  # parent dir does not exist yet -> must be created
port = free_port()
srv = Server(data, port)
try:
    st, js, hd, _ = call(port, "GET", "/api/health")
    check("health", st == 200 and js == {"status": "ok"})
    st, js, *_ = call(port, "GET", "/api/network")
    check("empty network", st == 200 and js["network"] == {"people": [], "friendships": []})

    # --- people
    st, js, *_ = call(port, "POST", "/api/people", {"name": "  Asha  "})
    check("add person -> 201 + trimmed", st == 201 and js["name"] == "Asha" and js["network"]["people"] == ["Asha"])
    st, js, *_ = call(port, "POST", "/api/people", {"name": "asha"})
    check("duplicate -> 409", st == 409 and not js["ok"] and "already" in js["error"])
    st, js, *_ = call(port, "POST", "/api/people", {"name": "   "})
    check("blank -> 400", st == 400)
    st, js, *_ = call(port, "POST", "/api/people", {"name": "x" * 31})
    check("too long -> 400", st == 400)
    st, js, *_ = call(port, "POST", "/api/people", {"nome": "x"})
    check("missing field -> 400", st == 400)
    st, js, *_ = call(port, "POST", "/api/people", raw_body="{not json")
    check("bad JSON -> 400", st == 400)
    st, js, *_ = call(port, "POST", "/api/people", raw_body=b'{"name":"\xff\xfe"}')
    check("invalid UTF-8 name -> 400", st == 400)
    st, js, *_ = call(port, "POST", "/api/people", {"name": 123})
    check("non-string name -> 400", st == 400)
    # names that need escaping come back as valid JSON
    st, js, *_ = call(port, "POST", "/api/people", {"name": '<b>"Zoë" \\ 😀'})
    check("tricky name stored & echoed as valid JSON", st == 201 and js["name"] == '<b>"Zoë" \\ 😀')

    # --- example + algorithms
    st, js, *_ = call(port, "POST", "/api/network/example")
    check("example loads 8 people / 8 edges", st == 200 and len(js["network"]["people"]) == 8 and len(js["network"]["friendships"]) == 8)
    st, js, *_ = call(port, "GET", "/api/route?from=Asha&to=Farah")
    check("route", js["path"] == ["Asha", "Chitra", "Esha", "Farah"])
    st, js, *_ = call(port, "GET", "/api/route?from=Asha&to=Gopal")
    check("route none -> null", st == 200 and js["path"] is None)
    st, js, *_ = call(port, "GET", "/api/route?from=Asha&to=Nobody")
    check("route unknown -> 404", st == 404)
    st, js, *_ = call(port, "GET", "/api/route?from=Asha&to=Asha")
    check("route same person -> 400", st == 400)
    st, js, *_ = call(port, "GET", "/api/common?a=Asha&b=Dev")
    check("common", js["common"] == ["Ben"])
    st, js, *_ = call(port, "GET", "/api/suggestions?name=Asha")
    check("suggestions", [s["person"] for s in js["suggestions"]] == ["Dev", "Esha"] and js["suggestions"][0]["mutual"] == ["Ben"])
    st, js, *_ = call(port, "GET", "/api/traverse?start=Asha&mode=bfs")
    check("traverse bfs", js["order"] == ["Asha", "Ben", "Chitra", "Dev", "Esha", "Farah"] and js["total"] == 8)
    st, js, *_ = call(port, "GET", "/api/traverse?start=Asha&mode=dfs")
    check("traverse dfs", js["order"] == ["Asha", "Ben", "Chitra", "Esha", "Dev", "Farah"])
    st, js, *_ = call(port, "GET", "/api/traverse?start=Asha&mode=zzz")
    check("traverse bad mode -> 400", st == 400)

    # --- percent-encoding / unicode names in query strings
    call(port, "POST", "/api/people", {"name": "Mary Ann"}); call(port, "POST", "/api/people", {"name": "José"})
    st, js, *_ = call(port, "POST", "/api/friendships", {"a": "Mary Ann", "b": "José"})
    check("friendship with spaces/unicode", st == 201)
    st, js, *_ = call(port, "GET", "/api/route?from=%s&to=%s" % (quote("Mary Ann"), quote("José")))
    check("percent-encoded query decoded", st == 200 and js["path"] == ["Mary Ann", "José"])
    st, js, *_ = call(port, "GET", "/api/route?from=Mary+Ann&to=Jos%C3%A9")
    check("'+' decodes to space", st == 200 and js["path"] == ["Mary Ann", "José"])
    st, js, *_ = call(port, "GET", "/api/route?from=%zz&to=x")
    check("bad percent-encoding -> 400", st == 400)

    # --- friendships
    st, js, *_ = call(port, "POST", "/api/friendships", {"a": "Asha", "b": "Ben"})
    check("duplicate friendship -> 409", st == 409)
    st, js, *_ = call(port, "POST", "/api/friendships", {"a": "Asha", "b": "Asha"})
    check("self friendship -> 400", st == 400)
    st, js, *_ = call(port, "DELETE", "/api/friendships?a=Asha&b=Ben")
    check("remove friendship", st == 200 and ["Asha", "Ben"] not in js["network"]["friendships"])
    st, js, *_ = call(port, "DELETE", "/api/friendships?a=Asha&b=Ben")
    check("remove again -> 404", st == 404)
    st, js, *_ = call(port, "DELETE", "/api/people?name=Hana")
    check("remove person", st == 200 and "Hana" not in js["network"]["people"])
    st, js, *_ = call(port, "DELETE", "/api/people?name=Hana")
    check("remove person again -> 404", st == 404)

    # --- routing & method handling
    st, js, hd, _ = call(port, "PUT", "/api/people", {"name": "x"})
    check("unsupported method rejected", st in (405, 501))
    st, js, hd, _ = call(port, "GET", "/api/people")
    check("wrong method on known route -> 405 + Allow", st == 405 and "POST" in hd.get("allow", ""))
    st, js, *_ = call(port, "GET", "/api/nope")
    check("unknown api -> 404 json", st == 404 and js and not js["ok"])
    st, _, hd, _ = call(port, "GET", "/api/network")
    check("api not cached", hd.get("cache-control") == "no-store" and hd.get("x-content-type-options") == "nosniff")
    st, _, hd, txt = call(port, "HEAD", "/api/health")
    check("HEAD has no body", st == 200 and txt == b"")

    # --- static files
    st, _, hd, txt = call(port, "GET", "/")
    check("serves index.html", st == 200 and b"Friend Finder" in txt and "text/html" in hd["content-type"])
    st, _, hd, _ = call(port, "GET", "/js/app.js")
    check("serves js with correct mime", st == 200 and "javascript" in hd["content-type"])
    st, _, hd, _ = call(port, "GET", "/css/style.css")
    check("serves css with correct mime", st == 200 and "text/css" in hd["content-type"])
    for p in ["/backend/src/main.cpp", "/backend/Makefile", "/package.json", "/netlify.toml", "/.git/config",
              "/data/network.json", "/css/../package.json", "/css/%2e%2e/package.json", "/js/..%2fpackage.json",
              "/%2e%2e/%2e%2e/etc/passwd", "/js/%00.js", "/js/..\\package.json", "/js/", "/css/nothing.css"]:
        st, _, _, txt = call(port, "GET", p)
        check("blocked: " + p, st in (400, 404) and b"root:" not in txt and b'"name": "friend-finder"' not in txt, "got %s" % st)
    check("path traversal raw request", b"root:" not in raw(port, b"GET /../../../../etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n"))

    # --- malformed / hostile requests
    check("garbage request line -> 400", raw(port, b"HELLO\r\n\r\n").startswith(b"HTTP/1.1 400"))
    check("bad http version -> 400", raw(port, b"GET / HTTP/9.9\r\n\r\n").startswith(b"HTTP/1.1 400"))
    check("chunked rejected", raw(port, b"POST /api/people HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").startswith(b"HTTP/1.1 400"))
    check("negative content-length -> 400", raw(port, b"POST /api/people HTTP/1.1\r\nContent-Length: -5\r\n\r\n").startswith(b"HTTP/1.1 400"))
    check("huge content-length -> 413", raw(port, b"POST /api/people HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n").startswith(b"HTTP/1.1 413"))
    big = b'{"name":"' + b"a" * 70000 + b'"}'
    check("oversized body -> 413", raw(port, b"POST /api/people HTTP/1.1\r\nContent-Length: %d\r\n\r\n" % len(big) + big).startswith(b"HTTP/1.1 413"))
    check("oversized headers -> 431", raw(port, b"GET / HTTP/1.1\r\nX: " + b"a" * 20000 + b"\r\n\r\n").startswith(b"HTTP/1.1 431"))
    check("client closes before full body -> 400", raw(port, b"POST /api/people HTTP/1.1\r\nContent-Length: 50\r\n\r\n{}", half_close=True).startswith(b"HTTP/1.1 400"))
    check("server still alive after abuse", call(port, "GET", "/api/health")[0] == 200)

    # --- concurrency: 60 parallel adds + friendships must all land, no corruption
    call(port, "POST", "/api/network/reset")
    errs = []
    def add(i):
        st, js, *_ = call(port, "POST", "/api/people", {"name": "P%02d" % i})
        if st != 201: errs.append((i, st))
    ts = [threading.Thread(target=add, args=(i,)) for i in range(60)]
    [t.start() for t in ts]; [t.join() for t in ts]
    st, js, *_ = call(port, "GET", "/api/network")
    check("60 concurrent adds all succeed", not errs and len(js["network"]["people"]) == 60, str(errs[:3]))
    ts = [threading.Thread(target=lambda i=i: call(port, "POST", "/api/friendships", {"a": "P%02d" % i, "b": "P%02d" % ((i + 1) % 60)})) for i in range(60)]
    [t.start() for t in ts]; [t.join() for t in ts]
    st, js, *_ = call(port, "GET", "/api/network")
    check("60 concurrent friendships (ring) all land", len(js["network"]["friendships"]) == 60)
    st, js, *_ = call(port, "GET", "/api/route?from=P00&to=P30")
    check("ring route has 31 nodes", len(js["path"]) == 31)

    # --- limits
    call(port, "POST", "/api/network/reset")
    codes = [call(port, "POST", "/api/people", {"name": "n%d" % i})[0] for i in range(205)]
    check("200-person cap enforced", codes.count(201) == 200 and codes.count(400) == 5)

    # --- persistence across restart
    call(port, "POST", "/api/network/example")
    call(port, "POST", "/api/people", {"name": "Zoë 😀"})
    rc = srv.stop()
    check("graceful shutdown exit code 0", rc == 0, str(rc))
    check("data file written (parent dir auto-created)", os.path.exists(data) and not os.path.exists(data + ".tmp"))
    srv = Server(data, port)
    st, js, *_ = call(port, "GET", "/api/network")
    check("state restored after restart", st == 200 and "Zoë 😀" in js["network"]["people"] and len(js["network"]["people"]) == 9 and len(js["network"]["friendships"]) == 8)
    check("insertion order restored", js["network"]["people"][:3] == ["Asha", "Ben", "Chitra"])
    srv.stop()

    # --- corrupt data file -> starts empty instead of crashing
    open(data, "w").write("{{{ not json")
    srv = Server(data, port)
    st, js, *_ = call(port, "GET", "/api/network")
    check("corrupt data file tolerated", st == 200 and js["network"]["people"] == [])
    srv.stop()

    # --- slow / stalled clients get 408 instead of holding a thread forever
    srv = Server(data, port, ["--timeout", "1"])
    t0 = time.time()
    out = raw(port, b"POST /api/people HTTP/1.1\r\nContent-Length: 50\r\n\r\n{}", timeout=6)
    check("stalled body -> 408 within ~timeout", out.startswith(b"HTTP/1.1 408") and time.time() - t0 < 4, out[:30])
    t0 = time.time()
    out = raw(port, b"GET / HTTP/1.1\r\nHost", timeout=6)
    check("stalled headers -> 408 within ~timeout", out.startswith(b"HTTP/1.1 408") and time.time() - t0 < 4, out[:30])
    t0 = time.time()
    s = socket.create_connection(("127.0.0.1", port), timeout=6)  # connects, sends nothing at all
    try: d = s.recv(10)
    except socket.timeout: d = b"TIMEOUT"
    s.close()
    check("silent connection is dropped", d == b"" and time.time() - t0 < 4, d)
    # 100 idle connections must not block real requests
    idle = [socket.create_connection(("127.0.0.1", port)) for _ in range(100)]
    check("API still responsive with 100 idle connections", call(port, "GET", "/api/health")[0] == 200)
    [c.close() for c in idle]
    srv.stop()

    # --- CORS only when configured
    srv = Server(data, port, ["--cors", "https://friendfinderr.netlify.app"])
    st, _, hd, _ = call(port, "OPTIONS", "/api/people", headers={"Origin": "https://friendfinderr.netlify.app", "Access-Control-Request-Method": "POST"})
    check("CORS preflight 204 + headers", st == 204 and hd.get("access-control-allow-origin") == "https://friendfinderr.netlify.app" and "POST" in hd.get("access-control-allow-methods", ""))
    st, _, hd, _ = call(port, "GET", "/api/health")
    check("CORS header on normal response", hd.get("access-control-allow-origin") == "https://friendfinderr.netlify.app")
    srv.stop()
    srv = Server(data, port)
    st, _, hd, _ = call(port, "GET", "/api/health")
    check("no CORS header by default", "access-control-allow-origin" not in hd)
finally:
    srv.stop()
    log = open(LOG, "rb").read().decode("utf-8", "replace") if os.path.exists(LOG) else ""
    bad = [l for l in log.splitlines() if any(k in l for k in ("Sanitizer", "WARNING:", "runtime error", "ERROR:", "handler error"))]
    check("server log has no sanitizer reports / handler errors", not bad, "\n".join(bad[:5]))
    shutil.rmtree(tmp, ignore_errors=True)

print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
