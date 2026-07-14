# =============================================================================
# Exchange — Multi-stage Docker Build
# Base: Ubuntu 26.04 (Resolute Raccoon) to match the host environment
#
# Stage 1 (builder):  Installs all build-time deps, compiles C++ services,
#                     agents, components, tests, and the React frontend.
# Stage 2 (runtime):  Minimal runtime image with only shared libraries needed
#                     to run the compiled binaries + nginx to serve the frontend.
#
# NOTE: eBPF tools (lat-tracer, total-lat, ebpf-msg-flow) are NOT included.
#       They require host-kernel BTF and privileged access — run them on the
#       bare-metal host, not inside a container.
# =============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Builder
# ---------------------------------------------------------------------------
FROM ubuntu:26.04 AS builder

# Avoid interactive prompts during apt-get install
ENV DEBIAN_FRONTEND=noninteractive

# Install build-time dependencies
# These are the exact packages from scripts/install-requirements, pinned to
# Ubuntu 26.04 (Resolute) repositories for reproducibility.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    pkg-config \
    # Boost (header-only: Beast, Asio — no runtime .so needed)
    libboost-dev \
    # OpenSSL
    libssl-dev \
    # Compression
    zlib1g-dev \
    # SQLite (default DB backend)
    libsqlite3-dev \
    # FlatBuffers (schema compiler + headers)
    flatbuffers-compiler \
    libflatbuffers-dev \
    # Google Test (for `make test`)
    libgtest-dev \
    googletest \
    libgmock-dev \
    # Logging
    libspdlog-dev \
    libfmt-dev \
    # USDT tracepoints (sys/sdt.h header for DTRACE_PROBE macros)
    systemtap-sdt-dev \
    # Node.js + npm (for frontend build)
    nodejs \
    npm \
    # Make
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# ---- Copy sources ----
# Copy dependency/schema files first for better layer caching
COPY fbs/ fbs/
COPY include/ include/
COPY Makefile .

# Copy C++ sources
COPY src/ src/
COPY app/ app/
COPY performance/*.cpp performance/

# Copy test sources
COPY tests/ tests/
COPY scripts/ scripts/

# Copy web frontend sources
COPY web/package.json web/package-lock.json web/
COPY web/tsconfig.json web/tsconfig.app.json web/tsconfig.node.json web/
COPY web/vite.config.ts web/
COPY web/eslint.config.js web/
COPY web/index.html web/
COPY web/Makefile web/
COPY web/src/ web/src/
COPY web/public/ web/public/

# ---- Build everything (except eBPF) ----
# 1. Generate FlatBuffers C++ and TypeScript headers
# 2. Build all C++ targets (services, agents, components, perf tools)
# 3. Build the web frontend (npm install + npm run build)
#
# eBPF is skipped — it requires host kernel BTF (/sys/kernel/btf/vmlinux)
# and bpftool, which are not available during docker build.
#
# The Makefile's 'all' target includes eBPF, so we build each sub-target
# explicitly instead.
RUN make fbs \
    && make \
        $(ls app/services/*.cpp     | sed 's|app/services/\(.*\)\.cpp|build/services/\1|') \
        $(ls app/client-agents/*.cpp | sed 's|app/client-agents/\(.*\)\.cpp|build/client-agents/\1|') \
        # $(ls app/components/*.cpp   2>/dev/null | sed 's|app/components/\(.*\)\.cpp|build/components/\1|') \
        # $(ls app/client-perf/*.cpp  2>/dev/null | sed 's|app/client-perf/\(.*\)\.cpp|build/client-perf/\1|') \
        # $(ls performance/*.cpp      2>/dev/null | sed 's|performance/\(.*\)\.cpp|build/performance/\1|') \
    && make -C web

# ---- Run tests (disabled — run via CI instead) ----
# RUN make $(ls tests/*.cpp | sed 's|tests/\(.*\)\.cpp|build/tests/\1|') \
#     && for test_bin in build/tests/*; do \
#          [ -x "$test_bin" ] && echo "Running $test_bin" && "$test_bin" || exit $?; \
#        done

# ---------------------------------------------------------------------------
# Stage 2: Runtime
# ---------------------------------------------------------------------------
FROM ubuntu:26.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install only the shared libraries needed at runtime (from ldd analysis)
# plus nginx for the reverse proxy.
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Runtime shared libraries (identified via ldd on the compiled binaries)
    libssl3t64 \
    libsqlite3-0 \
    libfmt10 \
    zlib1g \
    libzstd1 \
    # Nginx (reverse proxy + static frontend serving)
    nginx \
    # Useful for ops
    procps \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/exchange

# ---- Copy compiled binaries from builder ----
COPY --from=builder /build/build/services/ build/services/
COPY --from=builder /build/build/client-agents/ build/client-agents/
# COPY --from=builder /build/build/components/ build/components/
# COPY --from=builder /build/build/client-perf/ build/client-perf/
# COPY --from=builder /build/build/performance/ build/performance/

# ---- Copy frontend dist ----
COPY --from=builder /build/web/dist/ web/dist/

# ---- Copy runtime scripts & configs ----
COPY run-services run-services
COPY run-mm-native run-mm-native
COPY kill-all kill-all
COPY scripts/system-tuning.sh scripts/system-tuning.sh
COPY scripts/test-correctness scripts/test-correctness
COPY nginx/ nginx/

# ---- Copy seed data ----
COPY data/ data/

# ---- Create runtime directories ----
RUN mkdir -p log/execution-journals

# ---- Configure nginx ----
# Bake the frontend dist path into the nginx config
RUN sed 's|__FRONTEND_DIST_PATH__|/opt/exchange/web/dist|g' \
      nginx/exchange.conf > /etc/nginx/sites-available/exchange \
    && rm -f /etc/nginx/sites-enabled/default \
    && ln -sf /etc/nginx/sites-available/exchange /etc/nginx/sites-enabled/exchange

# ---- Ports ----
# 80    — Nginx (HTTP frontend + WS proxy)
# 9001  — Client Manager WebSocket (direct, if needed)
# 9002  — Market Data Server WebSocket (direct, if needed)
# 8080  — Order Entry REST API (direct, if needed)
# 8081  — Public Data REST API (direct, if needed)
EXPOSE 80 9001 9002 8080 8081

# ---- Entrypoint ----
# Start nginx in background, then launch exchange services in foreground.
# Services use SHM (/dev/shm) for IPC — the container needs --ipc=host or
# an adequately sized /dev/shm (default 64MB is NOT enough).
COPY docker-entrypoint.sh /opt/exchange/docker-entrypoint.sh
RUN chmod +x /opt/exchange/docker-entrypoint.sh

ENTRYPOINT ["/opt/exchange/docker-entrypoint.sh"]

