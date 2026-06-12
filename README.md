## Summary

This is a complete project simulating exchange matching order requests

will include more and more parts including profiling and other checks.

## Requires

```sh
sudo apt install -y flatbuffers-compiler libflatbuffers-dev
sudo apt install -y libgtest-dev libgmock-dev
sudo apt install -y build-essential git libssl-dev zlib1g-dev libboost-all-dev
# eBPF monitoring tools dependencies
sudo apt install -y clang libbpf-dev bpftool
```

## Message flow

### Logon Phase

```mermaid
sequenceDiagram
    autonumber
    actor Client as Client API/APP
    participant CM as Client Manager
    participant DB as Client Database

    Client->>CM: Connect (WS Handshake)
    activate CM
    
    CM->>DB: 1. Pop pending responses
    DB-->>CM: Pending Executions list
    loop Send pending executions
        CM->>Client: WS ClientResponse (OrderResponse)
    end
    
    CM->>DB: 2. Get current open orders
    DB-->>CM: Open orders list
    loop Send open orders
        CM->>Client: WS ClientResponse (OrderResponse, ExecType=OrderStatus)
    end

    CM->>DB: 3. Get all positions (Cash & Assets)
    DB-->>CM: Positions list
    loop Send positions
        CM->>Client: WS ClientResponse (PositionResponse)
    end

    Note over CM: Mark session as ready
    CM->>Client: 4. Send Ready Frame (OrderResponse, ExecType=Complete)
    deactivate CM

    Note over Client: CRITICAL: Client MUST receive Ready Frame<br/>(ExecType=Complete) before sending any OrderRequest!
```

### Ready Phase

```mermaid
graph TD
    Client[Client API/APP] -->|1. HTTP /order| HTTP[http_accepter]
    Client -->|1. WS OrderRequest| CM[client_manager]
    
    HTTP -->|2. Enqueue| RB_REQ[ORDER_REQUEST SHM Ring Buffer]
    CM -->|2. Enqueue| RB_REQ
    
    RB_REQ -->|3. Dequeue| OC[Matching Engine]
    OC -->|4. ExecutionReport| RB_RESP[ORDER_RESPONSE SHM Ring Buffer]
    OC -->|4. L2 Update| RB_L2[L2_UPDATE_RING SHM Ring Buffer]
    OC -->|4. L3 Update| RB_L3[L3_UPDATE_RING SHM Ring Buffer]
    
    RB_RESP -->|5. Dequeue| CM
    CM -->|6. WS ClientResponse / DB| Client
    
    RB_L2 -->|5. Dequeue| L2P[l2_publisher]
    L2P -->|6. WS L2Update| Client
    
    RB_L3 -->|5. Dequeue| L3P[l3_publisher]
    L3P -->|6. WS L3Update| Client
```

### L2/L3 Update & Subscription Phase

```mermaid
sequenceDiagram
    autonumber
    actor Client as Client API/APP
    participant Pub as L2/L3 Publisher

    Client->>Pub: Connect (WS Handshake)
    activate Pub
    
    Note over Client, Pub: [TODO] Ability to request SNAPSHOT actively (without reconnecting)
    
    Client->>Pub: Subscribe / Request SNAPSHOT
    
    Pub->>Client: Send Empty Frame (Side = None)
    Note over Client: MUST clear local L2/L3 data store upon receiving Empty Frame
    
    Note over Pub: [TODO] Send SNAPSHOT guarantees sequence:<br/>BEST BID -> BEST ASK -> OTHER LAYER
    
    loop Send SNAPSHOT
        Pub->>Client: WS L2/L3 Update (Snapshot)
    end
    
    loop Send INCREMENTALS
        Pub->>Client: WS L2/L3 Update (Incremental)
    end
    deactivate Pub
```

## Data Flow and Client Protocol

    ---H> Send through HTTP Channel
    --WS> Send through WebSocket Channel
    --RB> Send through Ring Buffer

OrderRequest: Client ---H> HTTP Server --RB> ORDERBOOK_CORE
    
    OrderRequest in flatbuffers format

PreAcked:     HTTP Server ---H> Client

    Only inform Client that HTTP server received OrderRequest, futher information will be sent as Executions from CLIENT_MANAGEMENT.

Executions:   ORDERBOOK_CORE --RB> CLIENT_MANAGEMENT 
                             --WS> Client + ---> DB(Status=SENT)      (if Client loged in)
                             ----> DB(Status=PENDING)                 (if Client loged out)
    
    Connection built -> Client log in -> Check and send cached Executions -> accept request
    Acceptable Request: 1. OrderRequest in flatbuffers format
                        2. Current position and Cash
                        3. Current Pending Order Status
                        (Use union format flatbuffers, TODO)

L2 Update:    ORDERBOOK_CORE --RB> L2_PUBLISHER --WS> Client

    Connection built -> Accepting Request
    Acceptable Request: 1. Subscription -> receive SNAPSHOT -> receive INCREMENTALS
                        2. Unsubscription
                        3. Resend Subscription will be consider identically as the first time and send SNAPSHOT, in case client lost.

    SNAPSHOT: Empty frame ahead, same sturcture as INCREMENTALS
    Empty frame: Side=None
    RB: L2_UPDATE_RING

L3 Update:    Same as L2

    RB: L3_UPDATE_RING

## eBPF Latency Tracer (lat-tracer)

The `lat-tracer` eBPF program measures end-to-end latency of order requests by hooking into kernel networking functions and user-space application functions (uprobes). It breaks down the total latency into Kernel Network Overhead, Client Manager Processing, and Matching Engine Processing.

### Latency Tracing Workflow

![tcp_recvmsg (RX Path)](assets/img/tcp_recvmsg.svg)

#### 2. Client Manager Processing
![Client Manager Processing](assets/img/client_manager.svg)

#### 3. Matching Engine Processing
![Matching Engine Processing](assets/img/matching_engine.svg)

#### 4. TX Path & Userspace Aggregation (tcp_sendmsg)
![tcp_sendmsg (TX Path) & Userspace Aggregation](assets/img/tcp_sendmsg.svg)

#### Responsibilities

**Kernel Space (eBPF)**:
1. **Network Hooks**: Intercepts `tcp_recvmsg` (entry/exit) to parse incoming FlatBuffer payloads for `exec_id` and records the start timestamp. Intercepts `tcp_sendmsg` to parse outgoing responses and compute total latency.
2. **Application Hooks (Uprobes)**: Attaches to C++ functions in `ClientManager` and `OrderBook`. Computes the time spent inside the Matching Engine and the Client Manager.
3. **Data Aggregation**: Retrieves the latency components when the response is sent out, bundles them into a `latency_event`, and pushes them to User Space.

**User Space (C++)**:
1. **Setup**: Loads the eBPF object, attaches kprobes and uprobes, and sets up the Ring Buffer.
2. **Processing**: Polls the Ring Buffer for `latency_event` structures.
3. **Analytics & Display**: Calculates the pure kernel networking overhead by subtracting application latencies from the total latency. Aggregates data by execution type (New, Modify, Cancel) and calculates percentiles (p50, p90, p99, p999), printing a real-time table to standard output.


## For Agent

### Version control

- Don't push to remote unless I told you to explicitly.
- When requested pushing, make multiple commit depending on the change, seperate different functionalities into different commits.

