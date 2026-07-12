# OrderBook Snapshot & Recovery

## Motivation
Historically, if the Matching Engine went down, recovering the full state of the `OrderBook` required replaying all historical execution journals from the beginning of the day. This could take a significant amount of time as message volume grew. 

To improve recovery times, we've introduced a snapshotting mechanism. Instead of relying solely on the append-only execution log, we take a snapshot of all active limit orders currently sitting in the book whenever the execution journal file rolls over.

## How it Works

### 1. Journal Rollover Trigger
The snapshot process is triggered automatically by the `mmaplog::MmapWriter`. We added a `rollover_cb_` (rollover callback) to the writer. Whenever the current memory-mapped log file reaches its capacity limit and rolls over to a new file index `N`, this callback is fired. 

In `app/services/matching-engine.cpp`, the matching engine listens to this callback and triggers `engine.take_snapshot(N)`.

### 2. Taking the Snapshot (`take_snapshot`)
When `take_snapshot` is called, the `OrderBook` loops over all its active limit orders (both bids and asks) and writes them into a binary file named `snapshot_<symbol_id>_<N>.dat` alongside the journal files.

**Time Priority (FIFO) Preservation:**
Because the `OrderBook` iterates through the price levels starting from the oldest order (`dummy_head.next`) and traversing towards the newest order, the snapshot file naturally stores the orders in strict FIFO order. 

### 3. Loading the Snapshot (`load_snapshot`)
When restarting the Matching Engine, you can load the order book state directly from the `.dat` file using `load_snapshot(N)`. 

As the snapshot file is read sequentially, `insertOrderToLevel` is called for each order. Because this method always appends incoming orders to the tail of the doubly-linked list for a given price level, reading the snapshot sequentially perfectly reconstructs the exact time-priority queue position of every order.

## Recovery Flow
In the event of a system crash, the complete recovery procedure is now fully automated and built into `MatchingEngine::restore()`:

1. The engine scans the journal directory for the highest available snapshot index `k` (e.g., `snapshot-<symbol_id>-<k>`). `snapshot-k` represents the state exactly after processing all events in `journal-k`.
2. If found, it automatically calls `engine.load_snapshot(k)`. If no snapshots are found (the extreme case), it gracefully falls back to starting at `k = 0`.
3. It then opens the execution journal using `MmapReader` and seeks directly to the start of `journal-{k+1}`.
4. It reads every `OrderResponseT` and applies `restore_from_response(resp)` to the relevant order book.
   - **Deterministic Replay (Event Sourcing):** The `restore_from_response` function translates structural execution events (`New`, `Cancelled`, `Replaced`) back into their equivalent `OrderRequestT` (`OrderAction_New`, `OrderAction_Cancel`, `OrderAction_Modify`).
   - It then feeds this reconstructed request back into the standard `processRequest` flow under `recover_mode`. This mode suppresses duplicate response logging while natively regenerating the exact same deterministic fills and order book state.
5. It continues seamlessly through `journal-{k+2}` and so on, until it is completely caught up.

This means you can delete older journals up to `journal-k` safely, as long as you keep `snapshot-k` and the journals from `k+1` onwards!

## Market Data Server Recovery
The `MarketDataServer` uses an identical autonomous recovery strategy via `MarketDataServer::restore()`. It operates completely independently of the `MatchingEngine` but utilizes the exact same snapshot files:

1. On startup, it scans the journal directory for the highest available snapshot index `k`.
2. It parses the raw binary `OrderSnapshot` structs (defined in `Snapshot.hpp`) directly from `snapshot-<symbol_id>-<k>`.
3. For each limit order in the snapshot, it injects the order into its `MDOrderBook` (which inherits from `L3Book`) by firing an `ExecType_New` update.
4. It then opens the execution journal via its own `MmapReader`, seeking directly to the start of `journal-{k+1}`.
5. It seamlessly replays all subsequent `OrderResponseT` execution records through the standard `__update` pipeline, guaranteeing its internal L2 and L3 market data state precisely reflects the matching engine's state.
