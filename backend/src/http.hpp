// Small blocking HTTP/1.1 server (POSIX sockets, thread per connection, Connection: close).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ff::http {

struct Request {
  std::string method;                                // GET, POST, ...
  std::string path;                                  // percent-decoded, no query
  std::map<std::string, std::string> query;          // decoded; first value wins
  std::map<std::string, std::string> headers;        // lower-case names
  std::string body;
  std::string param(const std::string& k) const {
    auto it = query.find(k);
    return it == query.end() ? std::string() : it->second;
  }
};

struct Response {
  int status = 200;
  std::string contentType = "application/json; charset=utf-8";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

using Handler = std::function<Response(const Request&)>;

struct Options {
  std::string bind = "0.0.0.0";
  int port = 8080;
  std::string corsOrigin;          // empty = no CORS headers
  size_t maxBody = 64 * 1024;
  size_t maxHeader = 16 * 1024;
  int maxConnections = 128;
  int timeoutSeconds = 10;
};

bool percentDecode(const std::string& in, std::string& out, bool plusAsSpace);

class Server {
 public:
  explicit Server(Options o) : opt_(std::move(o)) {}
  void route(const std::string& method, const std::string& path, Handler h);
  void setFallback(Handler h) { fallback_ = std::move(h); }  // GET/HEAD for non-API paths
  int run();                                                  // blocks until SIGINT/SIGTERM

 private:
  struct Route { std::string method, path; Handler handler; };
  Options opt_;
  std::vector<Route> routes_;
  Handler fallback_;
  void handleConnection(int fd);
  Response dispatch(const Request& req);
};

Response json(int status, const std::string& body);

}  // namespace ff::http
