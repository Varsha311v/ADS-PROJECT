// Friend Finder backend: REST API + static file server. No third-party dependencies.
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "http.hpp"
#include "json.hpp"
#include "store.hpp"

using namespace ff;
using json::Value;
namespace fs = std::filesystem;

namespace {

http::Response ok(Value v, int status = 200) {
  Value o = Value::object();
  o.set("ok", Value::boolean(true));
  for (auto& kv : v.obj) o.set(kv.first, std::move(kv.second));
  return http::json(status, o.dump());
}
http::Response fail(int status, const std::string& msg) {
  Value o = Value::object();
  o.set("ok", Value::boolean(false)).set("error", Value::string(msg));
  return http::json(status, o.dump());
}
Value strings(const std::vector<std::string>& v) {
  Value a = Value::array();
  for (const auto& s : v) a.push(Value::string(s));
  return a;
}
// Parses the JSON object body; returns nullopt (and fills `err`) if invalid.
std::optional<Value> bodyObject(const http::Request& r, http::Response& err) {
  auto doc = json::parse(r.body);
  if (!doc || !doc->isObject()) { err = fail(400, "Send a JSON object body."); return std::nullopt; }
  return doc;
}

const char* mimeFor(const std::string& ext) {
  if (ext == ".html") return "text/html; charset=utf-8";
  if (ext == ".css") return "text/css; charset=utf-8";
  if (ext == ".js") return "text/javascript; charset=utf-8";
  if (ext == ".json") return "application/json; charset=utf-8";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".png") return "image/png";
  if (ext == ".txt") return "text/plain; charset=utf-8";
  return nullptr;
}

// Only serves index.html and files under /css and /js. Backend sources and data are never exposed.
http::Response serveStatic(const fs::path& root, const http::Request& req) {
  std::string p = req.path == "/" ? "/index.html" : req.path;
  bool allowed = p == "/index.html" || p.rfind("/css/", 0) == 0 || p.rfind("/js/", 0) == 0;
  if (!allowed || p.find("..") != std::string::npos || p.find('\\') != std::string::npos)
    return fail(404, "Not found.");
  fs::path file = fs::weakly_canonical(root / p.substr(1));
  std::string rootStr = fs::weakly_canonical(root).string();
  if (file.string().compare(0, rootStr.size(), rootStr) != 0) return fail(404, "Not found.");
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return fail(404, "Not found.");
  const char* mime = mimeFor(file.extension().string());
  if (!mime || fs::file_size(file, ec) > 5u * 1024 * 1024) return fail(404, "Not found.");
  std::ifstream in(file, std::ios::binary);
  std::stringstream ss; ss << in.rdbuf();
  http::Response r;
  r.contentType = mime;
  r.body = ss.str();
  r.headers.emplace_back("Cache-Control", "no-cache");
  return r;
}

std::string envOr(const char* k, const std::string& d) { const char* v = std::getenv(k); return v && *v ? v : d; }

}  // namespace

int main(int argc, char** argv) {
  http::Options opt;
  opt.port = std::atoi(envOr("PORT", "8080").c_str());
  opt.bind = envOr("BIND", "0.0.0.0");
  opt.corsOrigin = envOr("CORS_ORIGIN", "");
  opt.timeoutSeconds = std::atoi(envOr("TIMEOUT_SECONDS", "10").c_str());
  std::string staticDir = envOr("STATIC_DIR", "..");
  std::string dataFile = envOr("DATA_FILE", "data/network.json");

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) { std::cerr << "missing value for " << name << "\n"; std::exit(2); }
      return argv[++i];
    };
    if (a == "--port") opt.port = std::atoi(next("--port").c_str());
    else if (a == "--bind") opt.bind = next("--bind");
    else if (a == "--static") staticDir = next("--static");
    else if (a == "--data") dataFile = next("--data");
    else if (a == "--cors") opt.corsOrigin = next("--cors");
    else if (a == "--timeout") opt.timeoutSeconds = std::atoi(next("--timeout").c_str());
    else if (a == "--help" || a == "-h") {
      std::cout << "Usage: friend-finder [--port N] [--bind IP] [--static DIR] [--data FILE] [--cors ORIGIN] [--timeout SEC]\n"
                   "Env:   PORT BIND STATIC_DIR DATA_FILE CORS_ORIGIN TIMEOUT_SECONDS\n";
      return 0;
    } else { std::cerr << "unknown option: " << a << "\n"; return 2; }
  }
  if (opt.port < 1 || opt.port > 65535) { std::cerr << "invalid port\n"; return 2; }
  if (opt.timeoutSeconds < 1 || opt.timeoutSeconds > 300) { std::cerr << "invalid timeout\n"; return 2; }

  Store store(dataFile);
  http::Server srv(opt);

  srv.route("GET", "/api/health", [](const http::Request&) { return http::json(200, "{\"status\":\"ok\"}"); });

  srv.route("GET", "/api/network", [&](const http::Request&) {
    return store.with([&](Graph& g) { Value v = Value::object(); v.set("network", Store::networkJson(g)); return ok(std::move(v)); });
  });

  srv.route("POST", "/api/people", [&](const http::Request& r) {
    http::Response err; auto b = bodyObject(r, err); if (!b) return err;
    auto name = b->getString("name");
    if (!name) return fail(400, "Send {\"name\": \"...\"}.");
    return store.with([&](Graph& g) {
      Result res = g.addPerson(*name);
      if (!res.ok) return fail(res.conflict ? 409 : 400, res.error);
      store.save();
      Value v = Value::object();
      v.set("name", Value::string(res.name)).set("network", Store::networkJson(g));
      return ok(std::move(v), 201);
    });
  });

  srv.route("DELETE", "/api/people", [&](const http::Request& r) {
    std::string name = r.param("name");
    return store.with([&](Graph& g) {
      if (!g.removePerson(name)) return fail(404, "No such person.");
      store.save();
      Value v = Value::object(); v.set("network", Store::networkJson(g));
      return ok(std::move(v));
    });
  });

  srv.route("POST", "/api/friendships", [&](const http::Request& r) {
    http::Response err; auto b = bodyObject(r, err); if (!b) return err;
    auto a = b->getString("a"), c = b->getString("b");
    if (!a || !c) return fail(400, "Send {\"a\": \"...\", \"b\": \"...\"}.");
    return store.with([&](Graph& g) {
      Result res = g.addFriendship(*a, *c);
      if (!res.ok) return fail(res.conflict ? 409 : 400, res.error);
      store.save();
      Value v = Value::object(); v.set("network", Store::networkJson(g));
      return ok(std::move(v), 201);
    });
  });

  srv.route("DELETE", "/api/friendships", [&](const http::Request& r) {
    std::string a = r.param("a"), c = r.param("b");
    return store.with([&](Graph& g) {
      if (!g.removeFriendship(a, c)) return fail(404, "Those two are not friends.");
      store.save();
      Value v = Value::object(); v.set("network", Store::networkJson(g));
      return ok(std::move(v));
    });
  });

  srv.route("POST", "/api/network/example", [&](const http::Request&) {
    return store.with([&](Graph& g) {
      g = Graph::example(); store.save();
      Value v = Value::object(); v.set("network", Store::networkJson(g));
      return ok(std::move(v));
    });
  });

  srv.route("POST", "/api/network/reset", [&](const http::Request&) {
    return store.with([&](Graph& g) {
      g.clear(); store.save();
      Value v = Value::object(); v.set("network", Store::networkJson(g));
      return ok(std::move(v));
    });
  });

  srv.route("GET", "/api/common", [&](const http::Request& r) {
    std::string a = r.param("a"), b = r.param("b");
    return store.with([&](Graph& g) {
      if (!g.has(a) || !g.has(b)) return fail(404, "Unknown person.");
      if (a == b) return fail(400, "Choose two different people.");
      Value v = Value::object(); v.set("common", strings(g.commonFriends(a, b)));
      return ok(std::move(v));
    });
  });

  srv.route("GET", "/api/route", [&](const http::Request& r) {
    std::string from = r.param("from"), to = r.param("to");
    return store.with([&](Graph& g) {
      if (!g.has(from) || !g.has(to)) return fail(404, "Unknown person.");
      if (from == to) return fail(400, "Choose two different people.");
      auto p = g.shortestPath(from, to);
      Value v = Value::object();
      v.set("path", p ? strings(*p) : Value::null());
      return ok(std::move(v));
    });
  });

  srv.route("GET", "/api/suggestions", [&](const http::Request& r) {
    std::string name = r.param("name");
    return store.with([&](Graph& g) {
      if (!g.has(name)) return fail(404, "Unknown person.");
      Value arr = Value::array();
      for (const auto& s : g.suggestions(name)) {
        Value o = Value::object();
        o.set("person", Value::string(s.person)).set("mutual", strings(s.mutual));
        arr.push(std::move(o));
      }
      Value v = Value::object(); v.set("suggestions", std::move(arr));
      return ok(std::move(v));
    });
  });

  srv.route("GET", "/api/traverse", [&](const http::Request& r) {
    std::string start = r.param("start"), mode = r.param("mode");
    if (mode.empty()) mode = "bfs";
    if (mode != "bfs" && mode != "dfs") return fail(400, "mode must be bfs or dfs.");
    return store.with([&](Graph& g) {
      if (!g.has(start)) return fail(404, "Unknown person.");
      Value v = Value::object();
      v.set("order", strings(g.traverse(start, mode == "dfs")))
       .set("total", Value::number(static_cast<double>(g.people().size())));
      return ok(std::move(v));
    });
  });

  fs::path root(staticDir);
  srv.setFallback([root](const http::Request& r) { return serveStatic(root, r); });

  return srv.run();
}
