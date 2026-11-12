#!/bin/sh
# Builds the static site into ./dist for Netlify (publishes only the frontend, never backend sources).
# Optional env var API_URL points the site at a hosted C++ backend, e.g. https://friend-finder-api.onrender.com
set -eu
rm -rf dist
mkdir -p dist
cp -r index.html css js dist/
if [ -n "${API_URL:-}" ]; then
  case "$API_URL" in
    http://*|https://*) ;;
    *) echo "API_URL must start with http:// or https://" >&2; exit 1 ;;
  esac
  case "$API_URL" in
    *[!A-Za-z0-9:/._~-]*) echo "API_URL contains unsupported characters" >&2; exit 1 ;;
  esac
  printf 'window.FRIEND_FINDER_API = "%s";\n' "$API_URL" > dist/js/config.js
  echo "Frontend will use backend: $API_URL"
else
  echo "API_URL not set: frontend will run in the browser unless it is served by the C++ server."
fi
