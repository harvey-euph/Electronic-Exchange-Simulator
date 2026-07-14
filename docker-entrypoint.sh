#!/bin/bash
set -e

echo "=== Exchange Container Starting ==="

# Apply system tuning if running as root (privileged container)
if [ "$EUID" -eq 0 ] && [ "${SKIP_TUNING:-0}" != "1" ]; then
    source /opt/exchange/scripts/system-tuning.sh
    apply_system_tuning 2>/dev/null || echo "Warning: Some system tuning could not be applied (expected in containers)"
fi

# Start nginx in the background
nginx &

# Start exchange services (launches daemons in background, then exits)
/opt/exchange/run-services

echo "=== All services started. Container staying alive. ==="

# Keep the container alive. The main process must not exit or Docker
# will think the container has stopped.
# Use 'tail -f /dev/null' — it blocks forever and is signal-friendly.
exec tail -f /dev/null
