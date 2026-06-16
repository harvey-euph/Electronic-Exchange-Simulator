#!/bin/bash
set -e

# 1. Clean up any leftover shared memory segments from previous runs
echo "=== Cleaning up old shared memory segments ==="
rm -f /dev/shm/ORDER_REQUEST* \
      /dev/shm/ORDER_RESPONSE \
      /dev/shm/MARKET_DATA_RING \
      /dev/shm/EXCHANGE_TELEMETRY \
      /dev/shm/*TELEMETRY 2>/dev/null || true

# 2. Run the start script (compiles aren't run here; run-all orchestrates the compiled binaries)
echo "=== Starting C++ Backend Services and Client Agents ==="
./run-all

# 3. Wait a moment for logs to be created
sleep 2

# 4. Touch all log files to ensure they exist before calling tail
mkdir -p log
touch log/matching-engine.log \
      log/client-manager.log \
      log/market-data-server.log \
      log/http-accepter.log \
      log/public-data.log \
      log/mm-advanced.log \
      log/mm-native.log \
      log/stra-insider.log \
      log/stra-noise.log

# 5. Tail the log files to keep the container alive and output logs to stdout
echo "=== Services and Agents started. Streaming logs to Docker stdout... ==="
tail -f log/*.log
