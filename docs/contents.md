# 交易所技術文獻目錄 (Documentation Index)

本目錄彙整了本交易所系統的所有核心技術文獻。您可以依據您的角色與需求，參閱對應的開發與架構指南：

---

## 1. 基礎架構與系統設計 (`basics.md`)
**目標受眾**：核心系統開發者、系統架構師、基礎架構工程師
**內容大綱**：
- **系統總覽 (System Overview)**：
  - 核心微服務介紹 (Matching Engine, Client Manager, Market Data Server, Public Data)
  - 行程間通訊 (IPC)：Shared Memory Ring Buffer (Request) 與 mmap log (Response/Execution Journal) 的設計與原理
- **資料流向 (Data Flow)**：
  - 委託單的生命週期路徑 (Client -> CM -> Request Ring -> ME -> Response Ring (mmap log) -> MD/CM -> Client)
- **高可用性與災難復原 (HA & Crash Recovery)**：
  - Execution Journal 的落地機制
  - Snapshot 的觸發條件與讀取
  - ME 重啟時的 Deterministic State Replay
- **狀態管理與同步**：
  - Client / Server Sequence Number 實作
  - SQLite Database 的持久化角色 (Open Orders, Positions)

---

## 2. API 開發者指南 (`dev.md`)
**目標受眾**：量化交易員、造市商 (Market Maker)、演算法開發者
**內容大綱**：
- **連線與認證 (Connection & Auth)**：
  - WebSocket 端點與連線規格
  - 認證機制與訊息序列號 (Sequence Number)
- **請求格式 (Request Specification)**：
  - 發送委託 (New), 修改委託 (Modify), 取消委託 (Cancel) 之指令格式
- **回報格式與生命週期 (Response & ExecType Semantics)**：
  - 執行回報 (Execution Report) 的種類
  - 各個 `ExecType` 之下 `Price` 與 `Quantity` 的精確定義
  - 錯誤碼對照表 (Reject Codes)
- **本地狀態維護建議 (Best Practices)**：
  - 建議的本地 Order Book 維護策略

---

## 3. 前端介面開發與操作指南 (`web.md`)
**目標受眾**：Web 前端工程師、UI/UX 設計師、終端使用者
**內容大綱**：
- **架構與通訊 (Frontend Architecture)**：
  - RESTful API (查詢歷史與部位) 與 WebSocket (即時報價與推播) 之整合
- **公開市場資料 (Public Market Data)**：
  - L2 Order Book 深度推播 (Delta update vs Snapshot)
  - 即時成交紀錄 (Public Trades) 與 Ticker 資料
- **介面操作與使用者互動 (User Interactions)**：
  - **價格/數量快速填充**：點擊 Order Book 上的 BID/ASK 價格或數量時的行為
  - **修改與取消**：如何在畫面上快速修改訂單價格/數量 (Modify) 或撤單 (Cancel)
- **前端渲染建議 (Rendering Guidelines)**：
  - Order Book 頻繁更新的渲染優化 (減少不必要的 re-render)
  - 延遲 (Latency) 容忍與樂觀 UI 更新策略 (Optimistic UI)

---

## 4. 效能與基準測試 (`performance.md`)
**目標受眾**：系統優化工程師、效能研究員
**內容大綱**：
- **效能指標 (Key Metrics)**：系統延遲與吞吐量基準
- **分析工具與方法 (Methodology)**：
  - eBPF 微秒級延遲分析
  - Ring Buffer 監控與 Throughput 測試方法

---

## 5. 部署與維運 (`ops.md`)
**目標受眾**：系統管理員、SRE (Site Reliability Engineer)
**內容大綱**：
- **建置與啟動 (Build & Startup)**：服務啟動順序與環境需求
- **資料庫 Schema 說明**：SQLite 資料表結構解說
- **日誌與監控 (Logging & Monitoring)**
