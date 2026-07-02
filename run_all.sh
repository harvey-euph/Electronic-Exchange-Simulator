#!/bin/bash
for d in tests/*; do
    if [ -d "$d" ]; then
        echo "Processing $d"
        pkill -f "build/services/" >/dev/null 2>&1 || true
        pkill -f "build/components/" >/dev/null 2>&1 || true
        rm -f /dev/shm/ORDER_REQUEST /dev/shm/ORDER_RESPONSE /dev/shm/MARKET_DATA_RING 2>/dev/null || true
        rm -f log/execution-journals/* 2>/dev/null
        
        ./build/services/matching-engine >/dev/null 2>&1 &
        ME_PID=$!
        sleep 0.5
        ./build/components/csv_inserter "$d/requests.csv"
        sleep 1
        kill -SIGINT $ME_PID 2>/dev/null || true
        wait $ME_PID 2>/dev/null || true
        
        timeout 1 ./build/components/execution-stdout > "$d/expected-exec.csv" 2>/dev/null || true
        ls -l "$d/expected-exec.csv"
    fi
done
