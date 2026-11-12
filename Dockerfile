# Builds the C++ backend and serves the frontend from the same container.
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ make && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY backend/ backend/
# Link libstdc++/libgcc statically so the runtime image needs nothing extra.
RUN make -C backend LDFLAGS="-pthread -static-libstdc++ -static-libgcc"

FROM ubuntu:24.04
RUN useradd --system --uid 10001 --home /app app && mkdir -p /app/site /data && chown app /data
WORKDIR /app
COPY --from=build /src/backend/build/friend-finder /app/friend-finder
COPY index.html /app/site/index.html
COPY css /app/site/css
COPY js /app/site/js
USER app
ENV PORT=8080 STATIC_DIR=/app/site DATA_FILE=/data/network.json
VOLUME /data
EXPOSE 8080
ENTRYPOINT ["/app/friend-finder"]
