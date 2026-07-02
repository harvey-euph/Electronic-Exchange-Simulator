#!/bin/bash
PROJECT_ROOT="$(pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

for d in tests/*; do
    if [ -d "$d" ] && [ -f "$d/requests.csv" ]; then
        echo "Generating expected-exec.csv for $d"
        mkdir -p "$PROJECT_ROOT/log"
        pkill -f "build/services/" >/dev/null 2>&1 || true
        pkill -f "build/components/" >/dev/null 2>&1 || true
        rm -f /dev/shm/ORDER_REQUEST /dev/shm/ORDER_RESPONSE /dev/shm/MARKET_DATA_RING 2>/dev/null || true
        rm -f "$PROJECT_ROOT/log/execution-journals/"* 2>/dev/null
        
        "$BUILD_DIR/services/matching-engine" >/dev/null 2>&1 &
        ME_PID=$!
        sleep 0.5
        "$BUILD_DIR/components/csv_inserter" "$d/requests.csv"
        sleep 1
        kill -SIGINT $ME_PID 2>/dev/null
        wait $ME_PID 2>/dev/null
        
        timeout 1 "$BUILD_DIR/components/execution-stdout" > "$d/expected-exec.csv" 2>/dev/null || true
    fi
done
