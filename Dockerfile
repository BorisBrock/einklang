# =============================================================================
# Stage 1 — Builder
# =============================================================================
# Uses a full GCC toolchain + CMake to compile EinklangServerApp from source.
# All heavy build dependencies stay in this layer and never reach the runtime
# image.
FROM debian:trixie-slim AS builder

# Install build tools (GCC 14 ships with trixie and provides <print>)
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-14 \
    g++-14 \
    cmake \
    git \
    ca-certificates \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the full source tree
COPY CMakeLists.txt .
COPY src/ src/

# Version compiled into the server; CI passes the release tag's version.
# Without it the build reports 0.0.0-dev (.git is not in the build context).
ARG EINKLANG_VERSION=""

# Configure — Release build, server target only.
# Saucer (GUI/webview) is a client-only dependency; the server does not use it.
# We disable it explicitly so it (and the audio libraries) are not fetched.
RUN cmake -S . -B /build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DFLATBUFFERS_BUILD_FLATC=ON \
    -DFLATBUFFERS_BUILD_TESTS=OFF \
    -DEINKLANG_BUILD_CLIENT=OFF \
    -DEINKLANG_VERSION="${EINKLANG_VERSION}"

# Build only the server executable
RUN cmake --build /build --target EinklangServerApp --parallel

# =============================================================================
# Stage 2 — Runtime
# =============================================================================
# Minimal Debian image; only the shared-library runtime deps are installed.
FROM debian:trixie-slim AS runtime

LABEL org.opencontainers.image.title="EinklangServerApp" \
      org.opencontainers.image.description="Einklang voice & chat server" \
      org.opencontainers.image.source="https://github.com/BorisBrock/einklang"

# libstdc++ and libgcc are already present in bookworm-slim.
# Nothing else is dynamically linked by the server (enet, flatbuffers,
# yaml-cpp and sqlite3 are all compiled statically into the binary).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    gosu \
    && rm -rf /var/lib/apt/lists/*

# Non-root user for security — the entrypoint drops privileges to this user
# after fixing /data ownership, so the image itself runs as root until exec.
RUN useradd --no-create-home --shell /usr/sbin/nologin einklang

# /app  — immutable; holds the binary, default config template, and entrypoint script
# /data — mutable; holds the live server-config.yml and einklang.db (mount this to a host folder)
WORKDIR /app

# Copy compiled binary from the builder stage
COPY --from=builder /build/EinklangServerApp ./EinklangServerApp
RUN chmod +x ./EinklangServerApp

# Store the default config as a read-only template inside the image.
# The entrypoint script copies it to /data on first run if no config exists there.
COPY src/server/server-config.yml ./server-config.default.yml

# Entrypoint script — seeds /data/server-config.yml and then exec's the server
COPY docker-entrypoint.sh ./docker-entrypoint.sh
RUN chmod +x ./docker-entrypoint.sh

# Create /data — ownership is fixed at runtime by the entrypoint so that
# host-mounted volumes (which override image-layer ownership) are handled.
RUN mkdir -p /data

# ENet default port (UDP) — mirrors the default in server-config.yml
EXPOSE 24816/udp

# Both server-config.yml and einklang.db are resolved relative to the working
# directory at runtime.  Setting WORKDIR to /data means mounting a single
# host directory there gives access to both files.
WORKDIR /data

VOLUME ["/data"]

# Trap SIGTERM gracefully (already handled in server.cpp via signalHandler)
STOPSIGNAL SIGTERM

# Healthy only if the server actually answers a status query on its UDP port
# (the port is read from /data/server-config.yml).
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD ["/app/EinklangServerApp", "--healthcheck"]

ENTRYPOINT ["/app/docker-entrypoint.sh"]
