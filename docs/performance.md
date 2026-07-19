# 效能與基準測試 (Performance & Benchmarking)

本交易所系統在設計之初即以「超低延遲 (Ultra-Low Latency)」與「高吞吐量 (High Throughput)」為最高指導原則。本文件說明系統的效能指標、量測方法，以及如何使用 eBPF 進行微秒等級的效能剖析。

---

## 1. 效能指標 (Key Metrics)

> **深入分析**：在極低延遲系統中，作業系統排程常常成為最大的干擾源。關於如何透過 eBPF 分析 OS Jitter，以及如何在 Server 上透過 `isolcpus` 達成完全隔離的實務做法，請參考 [OS Jitter 與核心搶占分析 (os-jitter.md)](os-jitter.md)。

以下是我們在隔離環境下 (CPU Pinning, 關閉 C-states, 獨佔核心) 測得的核心延遲數據：

### 端到端延遲 (Client E2E Latency)
*(單位：微秒 μs)*
| 操作類別 | P50 | P90 | P99 | P99.9 |
| :--- | :--- | :--- | :--- | :--- |
| **New (新增委託)** | 134.5 | 175.4 | 349.7 | 665.4 |
| **Modify (修改委託)** | 127.8 | 161.0 | 324.4 | 664.9 |
| **Cancel (取消委託)** | 176.2 | 308.3 | 470.7 | 776.8 |
| **整體平均 (ALL)** | 132.5 | 212.5 | 434.5 | 896.3 |

> **說明**：這是從 Client 端送出 TCP 封包，到收到 TCP Response 封包的完整來回時間 (Round-Trip Time)，包含所有的網路堆疊處理、JSON/內部格式 解析、撮合計算與資料庫持久化準備。

### 內部微服務延遲 (Internal Microservices)
*(單位：微秒 μs)*
| 元件 | P50 | P90 | P99 | 瓶頸點分析 |
| :--- | :--- | :--- | :--- | :--- |
| **Matching Engine (ME)** | 7.03 | 10.09 | 16.48 | 純粹的 L3 Order Book 搓合與記憶體操作。效能極佳，是系統中最快的環節。 |
| **Client Manager (CM)** | 23.6 | 29.5 | 108.7 | 包含 WebSocket frame 拆裝、權限檢查、內部格式組裝與非同步 SQLite 寫入準備。 |
| **Kernel 網路堆疊** | 9.64 | 13.62 | 25.81 | TCP 收發與 Socket buffer 拷貝時間。 |

---

## 2. 分析工具與方法 (Methodology)

傳統的應用程式 log 或 timestamp 無法精準測量微秒等級的延遲，且會干擾系統本身的效能 (Observer Effect)。因此，我們採用 **eBPF (Extended Berkeley Packet Filter)** 作為核心觀測工具。

### eBPF 探針分佈 (Probe Points)
我們在 `performance/ebpf` 目錄下實作了一套客製化的 BPF 追蹤器，結合了動態與靜態探針：

1. **Kernel Kprobes (動態探針)**：
   - 攔截 `tcp_recvmsg` 與 `tcp_sendmsg`，取得封包進出 Kernel 的絕對時間點 ($T_0$, $T_8$)。
   - BPF 程式會在 Kernel 內直接解析 WebSocket 標頭與內部二進位 payload，抓出 `exec_id` 作為追蹤 Key。
2. **USDT (User Statically-Defined Tracing) (靜態探針)**：
   - 在 `client-manager` 與 `matching-engine` 的 C++ 程式碼中埋入無成本的 DTRACE 探針。
   - 追蹤節點包含：`req_entry`, `req_enqueue`, `ob_req_entry`, `ob_resp_enqueue`, `exec_resp_entry` 等。

### 數據關聯與輸出
- BPF 程式使用 `BPF_MAP_TYPE_HASH` (`flow_events`) 將同一個 `exec_id` 在不同微服務間的時間戳串聯起來。
- 資料收集完成後，透過 Ring Buffer 傳遞到 Userspace 的分析腳本，產出各個 Pipeline Stage (0 -> 8) 的 P50/P90/P99 統計分佈表。

---
