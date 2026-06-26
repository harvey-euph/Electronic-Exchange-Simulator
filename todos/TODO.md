# Exchange Project TODO List

Based on the architectural review and current codebase state, here are the major areas for improvement across the system:

## 1. System Architecture & Performance
*   **Database Decoupling (Critical Path Optimization):**
    *   Implement the Async Persistence architecture outlined in `architecture-db-decoupling.md`.
    *   Shift the single source of truth to the Append-Only Mmap Log.
    *   Create a dedicated DB Writer consumer to handle Postgres updates asynchronously, removing I/O from the `ClientManager` critical path.
    *   Implement Read-Your-Own-Writes (RYOW) in the API layer utilizing a `Global_SeqNo` to prevent stale reads.
*   **Data Manager Service Extraction:**
    *   Follow the `data-manager-plan.md` to extract database queries into a new `data-manager` service.
    *   Establish `data_req_queue` and `data_resp_queue` SHM Ring Buffers.
    *   **Resolve MsgSeqNum Race Condition:** Choose and implement one of the proposed solutions (e.g., having Matching Engine assign the Global Sequence Number or making CM the source of truth).
*   **Order Memory Management (Low-Hanging Fruit):**
    *   In `src/OrderBook.cpp`, the `createOrder` function currently uses `new Order {...}` and `delete`. This dynamic heap allocation per order is a major latency bottleneck.
    *   **Fix:** Refactor `OrderBook` to utilize a custom Object Pool or the existing `Mempool.hpp` to pre-allocate `Order` structs, achieving zero-allocation during the critical matching phase.

## 2. Observability & eBPF
*   **Improve eBPF Latency Tracer Precision:**
    *   Update `lat-tracer` based on `adjust-tracepoint.md`.
    *   Migrate away from TID (`pid_tgid`) correlation for `tcp_sendmsg` to `net_dev_start_xmit`.
    *   Implement `skb` pointer correlation or TCP sequence number correlation `(sock*, tcp_seq)` to accurately measure exact TX stack latency, even when packets traverse `ksoftirqd` or other softirq contexts.

## 3. Frontend & Web Client
*   **UI/UX Aesthetic Enhancements:**
    *   The frontend handles high-frequency data throttling well, but the UI aesthetics can be improved. Introduce modern styling conventions (e.g., glassmorphism, dynamic micro-animations for order book updates).
    *   Implement CSS variables/tokens in `index.css` to build a standardized design system for dark mode and responsive layouts.
*   **Dependency Updates:**
    *   Ensure Vite and React dependencies are kept up-to-date. Implement proper strict typing across all WebSockets schemas (FlatBuffers TS generated code).

## 4. Code Maintenance & Testing
*   **Implement Unit & Stress Tests:**
    *   Ensure automated stress tests (`client-perf` and Chaos Stress Testers) are integrated into a CI/CD pipeline.
    *   Add isolated unit tests for `SHMRingBuffer` edge cases (e.g., wrapping behavior).
