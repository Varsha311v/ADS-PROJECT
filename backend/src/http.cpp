#include "http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>

namespace ff::http {
namespace {

volatile std::sig_atomic_t g_stop = 0;
std::atomic<int> g_active{0};

void onSignal(int) { g_stop = 1; }

const char* statusText(int s) {
  switch (s) {
    case 100: return "Continue"; case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
    case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
    case 408: return "Request Timeout"; case 409: return "Conflict"; case 411: return "Length Required";
    case 413: return "Payload Too Large"; case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error"; case 503: return "Service Unavailable";
    default: return "Unknown";
  }
}

bool sendAll(int fd, const char* p, size_t n) {
  while (n) {
    ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
    if (w < 0) { if (errno == EINTR) continue; return false; }
    p += w; n -= static_cast<size_t>(w);
  }
  return true;
}

std::string lowerStr(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  return s;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

bool percentDecode(const std::string& in, std::string& out, bool plusAsSpace) {
  out.clear();
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (c == '%') {
      if (i + 2 >= in.size()) return false;  // need two hex digits after '%'
      auto hv = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
      };
      int hi = hv(in[i + 1]), lo = hv(in[i + 2]);
      if (hi < 0 || lo < 0) return false;
      out += static_cast<char>(hi * 16 + lo);
      i += 2;
    } else if (c == '+' && plusAsSpace) out += ' ';
    else out += c;
  }
  return true;
}

Response json(int status, const std::string& body) {
  Response r; r.status = status; r.body = body; return r;
}

void Server::route(const std::string& method, const std::string& path, Handler h) {
  routes_.push_back({method, path, std::move(h)});
}

Response Server::dispatch(const Request& req) {
  bool pathKnown = false;
  std::string allow;
  for (const auto& r : routes_) {
    if (r.path != req.path) continue;
    pathKnown = true;
    allow += (allow.empty() ? "" : ", ") + r.method;
    if (r.method == req.method || (req.method == "HEAD" && r.method == "GET")) return r.handler(req);
  }
  if (req.method == "OPTIONS" && pathKnown) { Response r; r.status = 204; r.body.clear(); return r; }
  if (pathKnown) {
    Response r = json(405, "{\"ok\":false,\"error\":\"Method not allowed.\"}");
    r.headers.emplace_back("Allow", allow + ", OPTIONS");
    return r;
  }
  if (req.path.rfind("/api/", 0) == 0) return json(404, "{\"ok\":false,\"error\":\"Not found.\"}");
  if (fallback_ && (req.method == "GET" || req.method == "HEAD")) return fallback_(req);
  return json(404, "{\"ok\":false,\"error\":\"Not found.\"}");
}

void Server::handleConnection(int fd) {
  auto started = std::chrono::steady_clock::now();
  // Lingering close: send FIN, then drain unread input briefly. Closing with unread data in the
  // receive buffer makes the kernel send RST, which can destroy the response we just wrote
  // (e.g. a 413 for an oversized upload) before the client has read it.
  struct Guard {
    int fd;
    ~Guard() {
      ::shutdown(fd, SHUT_WR);
      timeval t{1, 0};
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
      auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      char sink[4096];
      size_t drained = 0;
      while (drained < (1u << 20) && std::chrono::steady_clock::now() < until) {
        ssize_t n = ::recv(fd, sink, sizeof sink, 0);
        if (n <= 0) break;
        drained += static_cast<size_t>(n);
      }
      ::close(fd);
      --g_active;
    }
  } guard{fd};

  timeval tv{opt_.timeoutSeconds, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  auto fail = [&](int status, const char* msg) {
    std::string body = std::string("{\"ok\":false,\"error\":\"") + msg + "\"}";
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + statusText(status) +
                       "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    sendAll(fd, head.data(), head.size());
    sendAll(fd, body.data(), body.size());
  };

  // ---- read head ----
  std::string buf; char tmp[4096]; size_t headEnd;
  while ((headEnd = buf.find("\r\n\r\n")) == std::string::npos) {
    if (buf.size() > opt_.maxHeader) return fail(431, "Request headers too large.");
    ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n == 0) return;
    if (n < 0) { if (errno == EINTR) continue; if (errno == EAGAIN || errno == EWOULDBLOCK) { if (!buf.empty()) fail(408, "Request timed out."); } return; }
    buf.append(tmp, static_cast<size_t>(n));
  }
  if (headEnd > opt_.maxHeader) return fail(431, "Request headers too large.");

  Request req;
  std::string head = buf.substr(0, headEnd);
  req.body = buf.substr(headEnd + 4);

  size_t lineEnd = head.find("\r\n");
  std::string first = head.substr(0, lineEnd);
  size_t s1 = first.find(' '), s2 = first.rfind(' ');
  if (s1 == std::string::npos || s2 == s1) return fail(400, "Malformed request line.");
  req.method = first.substr(0, s1);
  std::string target = first.substr(s1 + 1, s2 - s1 - 1);
  std::string version = first.substr(s2 + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") return fail(400, "Unsupported HTTP version.");
  if (req.method != "GET" && req.method != "HEAD" && req.method != "POST" && req.method != "DELETE" &&
      req.method != "OPTIONS")
    return fail(405, "Method not allowed.");
  if (target.empty() || target[0] != '/') return fail(400, "Bad request target.");

  while (lineEnd != std::string::npos) {
    size_t next = head.find("\r\n", lineEnd + 2);
    std::string line = head.substr(lineEnd + 2, next == std::string::npos ? std::string::npos : next - lineEnd - 2);
    lineEnd = next;
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return fail(400, "Malformed header.");
    req.headers[lowerStr(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
  }

  // ---- target: path + query ----
  size_t q = target.find('?');
  std::string rawPath = target.substr(0, q);
  if (!percentDecode(rawPath, req.path, false)) return fail(400, "Bad percent-encoding.");
  if (req.path.find('\0') != std::string::npos) return fail(400, "Bad path.");
  if (q != std::string::npos) {
    std::string qs = target.substr(q + 1);
    size_t pos = 0;
    while (pos <= qs.size()) {
      size_t amp = qs.find('&', pos);
      std::string pair = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      pos = amp == std::string::npos ? qs.size() + 1 : amp + 1;
      if (pair.empty()) continue;
      size_t eq = pair.find('=');
      std::string k, v;
      if (!percentDecode(pair.substr(0, eq), k, true) ||
          !percentDecode(eq == std::string::npos ? "" : pair.substr(eq + 1), v, true))
        return fail(400, "Bad percent-encoding.");
      req.query.emplace(std::move(k), std::move(v));  // first wins
    }
  }

  // ---- body ----
  if (req.headers.count("transfer-encoding")) return fail(400, "Chunked bodies are not supported.");
  size_t want = 0;
  auto cl = req.headers.find("content-length");
  if (cl != req.headers.end()) {
    if (cl->second.empty() || cl->second.size() > 9 || cl->second.find_first_not_of("0123456789") != std::string::npos)
      return fail(400, "Bad Content-Length.");
    want = std::stoul(cl->second);
    if (want > opt_.maxBody) return fail(413, "Request body too large.");
  } else if ((req.method == "POST") && !req.body.empty()) {
    return fail(411, "Content-Length required.");
  }
  if (want > req.body.size()) {
    auto ex = req.headers.find("expect");
    if (ex != req.headers.end() && lowerStr(ex->second) == "100-continue") {
      static const char c100[] = "HTTP/1.1 100 Continue\r\n\r\n";
      sendAll(fd, c100, sizeof c100 - 1);
    }
  }
  while (req.body.size() < want) {
    ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n == 0) return fail(400, "Body ended early.");
    if (n < 0) { if (errno == EINTR) continue; return fail(408, "Request timed out."); }
    req.body.append(tmp, static_cast<size_t>(n));
  }
  if (req.body.size() > want) req.body.resize(want);

  // ---- dispatch ----
  Response res;
  try {
    res = dispatch(req);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "handler error: %s\n", e.what());
    res = json(500, "{\"ok\":false,\"error\":\"Internal server error.\"}");
  } catch (...) {
    res = json(500, "{\"ok\":false,\"error\":\"Internal server error.\"}");
  }

  std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + statusText(res.status) + "\r\n";
  out += "Content-Type: " + res.contentType + "\r\n";
  out += "Content-Length: " + std::to_string(res.status == 204 ? 0 : res.body.size()) + "\r\n";
  out += "Connection: close\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\n";
  if (req.path.rfind("/api/", 0) == 0) out += "Cache-Control: no-store\r\n";
  if (!opt_.corsOrigin.empty()) {
    out += "Access-Control-Allow-Origin: " + opt_.corsOrigin + "\r\n";
    out += "Vary: Origin\r\n";
    if (req.method == "OPTIONS") {
      out += "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Max-Age: 600\r\n";
    }
  }
  for (const auto& h : res.headers) out += h.first + ": " + h.second + "\r\n";
  out += "\r\n";
  sendAll(fd, out.data(), out.size());
  if (req.method != "HEAD" && res.status != 204) sendAll(fd, res.body.data(), res.body.size());

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  std::fprintf(stderr, "%s %s -> %d (%lld ms)\n", req.method.c_str(), req.path.c_str(), res.status, static_cast<long long>(ms));
}

int Server::run() {
  struct sigaction sa{};
  sa.sa_handler = onSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // no SA_RESTART so accept() wakes up on a signal
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  std::signal(SIGPIPE, SIG_IGN);

  int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (lfd < 0) { std::perror("socket"); return 1; }
  int yes = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(opt_.port));
  if (inet_pton(AF_INET, opt_.bind.c_str(), &addr.sin_addr) != 1) {
    std::fprintf(stderr, "invalid bind address: %s\n", opt_.bind.c_str());
    return 1;
  }
  if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { std::perror("bind"); return 1; }
  if (::listen(lfd, 128) < 0) { std::perror("listen"); return 1; }
  std::fprintf(stderr, "Friend Finder listening on http://%s:%d\n", opt_.bind.c_str(), opt_.port);

  while (!g_stop) {
    int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) {
      if (errno == EINTR || errno == ECONNABORTED) continue;
      if (errno == EMFILE || errno == ENFILE) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
      std::perror("accept");
      break;
    }
    if (g_active.load() >= opt_.maxConnections) {
      static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      sendAll(cfd, busy, sizeof busy - 1);
      ::close(cfd);
      continue;
    }
    ++g_active;
    try {
      std::thread([this, cfd] { handleConnection(cfd); }).detach();
    } catch (...) {
      --g_active;
      ::close(cfd);
    }
  }
  ::close(lfd);
  std::fprintf(stderr, "Shutting down...\n");
  for (int i = 0; i < 50 && g_active.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return 0;
}

}  // namespace ff::http
