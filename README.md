# Friend Finder

See how people are connected. Add people, make them friends, and explore the network the way "people you may know" works on social apps.

- **Frontend:** plain HTML/CSS/JS, draws the network as an SVG graph.
- **Backend:** C++17 REST API with no third-party dependencies. It owns the graph, runs BFS/DFS/set intersection, saves to disk, and also serves the frontend.
- **No backend?** The page still works: it detects that no server answers and runs the same logic in the browser (saved in `localStorage`).

Live demo (frontend, browser mode): https://friendfinderr.netlify.app/

## Features

- **Friends in common**: set intersection of two friend lists
- **How connected?**: shortest route between two people (BFS)
- **Who might they know?**: friends of friends, ranked by number of mutual friends
- **Visit everyone**: BFS (nearest first) or DFS (deep first) with visit numbers on the graph
- Three colour themes, click-to-choose nodes, keyboard and screen-reader friendly, responsive to phone width

## Quick start

Requirements: a C++17 compiler (g++ or clang++), `make`, Linux or macOS. Node 18+ and Python 3 are only needed for tests.

```bash
make -C backend run          # builds, then serves on http://localhost:8080
```

Open http://localhost:8080. The banner under the title says "Connected to the C++ server" when the backend is in use.

Frontend only, no backend: `npm run start:static` (or open `index.html`).

## Project layout

```
index.html, css/, js/       frontend
  js/store.js               picks the C++ API or the in-browser fallback
  js/graph.js               in-browser graph logic (fallback mode)
  js/config.js              API base URL (empty = same origin)
backend/
  src/graph.*               graph, BFS, DFS, set intersection, validation
  src/json.*                JSON parser/serializer
  src/http.*                HTTP/1.1 server (POSIX sockets, thread per connection)
  src/store.*               mutex-guarded graph + atomic file persistence
  src/main.cpp              routes, static files, CLI/env config
  tests/test_main.cpp       C++ unit tests
  tests/api_test.py         end-to-end API tests against the real binary
tests/                      JS unit tests and Playwright browser tests
scripts/netlify-build.sh    builds the static site for Netlify
Dockerfile                  backend + frontend in one image
```

## Configuration

| Flag | Env var | Default | Meaning |
| --- | --- | --- | --- |
| `--port` | `PORT` | `8080` | Port to listen on |
| `--bind` | `BIND` | `0.0.0.0` | IPv4 address to bind (`127.0.0.1` for local only) |
| `--static` | `STATIC_DIR` | `..` | Folder holding `index.html`, `css/`, `js/` |
| `--data` | `DATA_FILE` | `data/network.json` | Where the network is saved |
| `--cors` | `CORS_ORIGIN` | none | Allowed browser origin when the site is hosted elsewhere |
| `--timeout` | `TIMEOUT_SECONDS` | `10` | Per-connection read/write timeout |

## API

All responses are JSON with `"ok": true|false`. Errors carry `"error"` text. Mutations return the full updated `"network"`.

| Method and path | Body / query | Success | Errors |
| --- | --- | --- | --- |
| `GET /api/health` | | `{"status":"ok"}` | |
| `GET /api/network` | | `{"network":{"people":[...],"friendships":[[a,b],...]}}` | |
| `POST /api/people` | `{"name":"Maya"}` | 201 | 400 invalid name, 409 duplicate |
| `DELETE /api/people?name=Maya` | | 200 | 404 |
| `POST /api/friendships` | `{"a":"Maya","b":"Ben"}` | 201 | 400, 409 already friends |
| `DELETE /api/friendships?a=Maya&b=Ben` | | 200 | 404 |
| `POST /api/network/example` | | 200, loads the demo network | |
| `POST /api/network/reset` | | 200, removes everyone | |
| `GET /api/common?a=&b=` | | `{"common":[...]}` | 404 unknown person, 400 same person |
| `GET /api/route?from=&to=` | | `{"path":[...]}` or `{"path":null}` | 404, 400 |
| `GET /api/suggestions?name=` | | `{"suggestions":[{"person":"","mutual":[...]}]}` | 404 |
| `GET /api/traverse?start=&mode=bfs\|dfs` | | `{"order":[...],"total":N}` | 404, 400 bad mode |

```bash
curl -X POST localhost:8080/api/network/example
curl 'localhost:8080/api/route?from=Asha&to=Farah'
# {"ok":true,"path":["Asha","Chitra","Esha","Farah"]}
```

Limits: names are 1 to 30 characters (valid UTF-8, no control characters), up to 200 people and 5000 friendships, request bodies up to 64 KB. Duplicate-name checks are case-insensitive for ASCII letters only.

## Tests

```bash
npm test                 # JS unit tests + C++ unit tests + API integration tests
npm run test:e2e         # browser tests (needs: npm i --no-save playwright && npx playwright install chromium)
```

The API tests start the compiled server and cover validation, percent-encoded and Unicode names, path traversal attempts, malformed/oversized/stalled requests, 60 concurrent writers, persistence across restart, a corrupt data file, and CORS. CI also runs them under AddressSanitizer, UBSan and ThreadSanitizer.

## Deploy

The backend needs a host that runs a process or container (Render, Railway, Fly.io, a VPS). Netlify cannot run it.

**Backend + frontend together (simplest):**

```bash
docker build -t friend-finder .
docker run -p 8080:8080 -v ffdata:/data friend-finder
```

Platforms like Render set `PORT` automatically; use `/api/health` as the health check path. Mount a persistent disk at `/data` or the network resets whenever the container is replaced.

**Frontend on Netlify, backend elsewhere:**

1. Deploy the backend with `CORS_ORIGIN=https://your-site.netlify.app`.
2. In Netlify, set the environment variable `API_URL=https://your-backend.example.com`. `netlify.toml` already runs `scripts/netlify-build.sh`, which publishes only the frontend and writes the URL into `config.js`.

If the backend is unreachable or CORS is not set, the site quietly falls back to browser mode.

## Things to know before going public

- **One shared network.** The backend keeps a single network for everyone who visits. Anyone can add, remove or reset. That is fine for a demo or a class project; per-user sessions or accounts would be the next step for a real product.
- **No TLS in the server.** Run it behind a platform or reverse proxy that terminates HTTPS.
- **No authentication or rate limiting.** There are connection, size and timeout limits, but nothing per client.
- **Linux/macOS only.** The server uses POSIX sockets.

## Complexity

| Feature | Method | Time |
| --- | --- | --- |
| Add person or friendship | Insert into a map and a set | O(log V) |
| Friends in common | Set intersection | O(d1 + d2) |
| Shortest route | BFS with a queue | O(V + E) |
| Suggestions | Friends of friends | O(d²) |
| Visit everyone | BFS or DFS | O(V + E) |

V = people, E = friendships, d = friends of one person.

## License

MIT
