# Docker Usage Guide

## First Time Setup (Fresh Clone)

If you have just cloned the repository, getting the exchange running is just a single step.

1. Ensure port `80` on your host machine is free (stop any local `nginx` or Apache instances).
2. Run the build and start command:
   ```bash
   docker compose up --build -d
   ```
   *This first command will take several minutes as it installs all Ubuntu dependencies, compiles the C++ matching engine, and builds the React frontend inside the container.*

3. Verify it is running:
   ```bash
   docker compose ps
   ```

4. Check the logs to ensure the services started correctly:
   ```bash
   docker compose logs -f
   ```

5. Access the Web UI at `http://localhost`.

---

## Quick Reference

### Build

```bash
# Build the image (first time or after code changes)
docker build -t exchange:latest .

# Build with no cache (full rebuild, useful if deps changed)
docker build --no-cache -t exchange:latest .
```

### Start / Stop

```bash
# Start (uses existing image, no rebuild)
docker compose up

# Start in background (detached)
docker compose up -d

# Start with rebuild (after code changes)
docker compose up --build
docker compose up --build -d

# Stop (keeps volumes/data)
docker compose down

# Stop and delete all data (journals, DB, logs)
docker compose down -v
```

### Logs & Monitoring

```bash
# Follow all logs
docker compose logs -f

# Follow logs for the exchange service only
docker compose logs -f exchange

# Show last 100 lines
docker compose logs --tail 100

# Check container health status
docker compose ps
```

### Debugging

```bash
# Open a shell inside the running container
docker exec -it exchange bash

# Check running processes inside container
docker exec exchange ps aux

# Check shared memory usage
docker exec exchange ls -la /dev/shm/

# Check if services are actually running
docker exec exchange pgrep -a -f "build/services/"
```

### Market Maker (inside container)

```bash
# Start market makers
docker exec exchange ./run-mm-native

# Kill all services and market makers
docker exec exchange ./kill-all
```

### Data Management

```bash
# View database files
docker exec exchange ls -la /opt/exchange/data/

# View execution journals and logs
docker exec exchange ls -la /opt/exchange/log/
docker exec exchange ls -la /opt/exchange/log/execution-journals/

# Copy a database file out of the container to your host
docker cp exchange:/opt/exchange/data/clients.db ./clients.db
```

### Cleanup

```bash
# Remove stopped containers and dangling images
docker system prune

# Remove ALL unused images (reclaim disk space)
docker system prune -a

# Check disk usage
docker system df
```

---

## Architecture Notes

- All services run in a **single container** because they communicate via `/dev/shm` shared memory ring buffers.
- `docker-compose.yml` sets `shm_size: 1g` (default 64MB is too small for the ring buffers).
- Execution journals and DB files are persisted in Docker volumes — they survive `docker compose down` but are deleted by `docker compose down -v`.
- eBPF tools (`lat-tracer`, `total-lat`) require host kernel access — run them **on the host**, not inside the container.
