# Data Manager 重構計畫與待辦事項

## 1. 重構目標
引入全新的 `data-manager` (DM) 服務，作為系統中**唯一**與資料庫 (ClientDatabase) 互動的服務。將原先由 `ClientManager` (CM) 處理的資料存取職責完全剝離，藉此達到更高的系統解耦與可能率能提升。

## 2. 系統架構變更
*   **通訊機制建立：** 
    *   建立兩條基於 `SHMRingBuffer` 的 IPC 通道：`data_req_queue` 與 `data_resp_queue`，提供 CM 與 DM 進行非同步通訊。
    *   通訊 Payload 直接使用 FlatBuffers 定義的 `ClientRequest` (寫入 req_queue) 與 `ClientResponse` (寫入 resp_queue) 序列化二進位資料。
*   **DataManager 職責：**
    *   透過 `mmaplog::MmapReader` 獨立背景讀取 `response_ring_` (來自 Matching Engine 的 execution)。
    *   將讀取到的 `OrderResponseT` 寫入 DB (取代原先 `ClientManager::handle_execution_response` 中的 `db_->update_on_execution`)。
    *   監聽 `data_req_queue`，處理來自 CM 的非同步請求 (LogOn, PositionRequest, OpenOrderRequest)。
*   **ClientManager 職責變更：**
    *   完全移除對 `ClientDatabase` 的依賴 (`std::shared_ptr<ClientDatabase> db`)。
    *   遇到需要查 DB 的請求時，將 `ClientRequest` 序列化並 push 到 `data_req_queue`。
    *   在原有的 `poll_server()` 迴圈中，新增對 `data_resp_queue` 的 Busy-Polling 邏輯。當取得到資料時，反序列化成 `ClientResponse` 並透過 WebSocket 轉發給對應的 client。
*   **不定長度資料處理 (Open Orders 等)：**
    *   當 DM 需要回傳多筆資料時，先逐筆發送對應的資料。
    *   最後送出一筆 `ClientResponse` 包含 `OrderResponse(ExecType = Complete)` 作為結束訊號，讓 CM 知道該批次處理完畢。

## 3. Logon 流程細節 (User Login Resume)
當 CM 收到 Client Logon 請求：
1.  CM 將 `AdminRequest(LogOn)` 轉發至 `data_req_queue`。
2.  DM 收到後查詢 DB 進行 SeqNum 驗證：
    *   **Ack SeqNum 落後：** 從 DB 撈出 Missed Responses，包裝成 `ClientResponse` 丟入 `data_resp_queue`，最後再發送一個 `Logon Accept (Ready)`。
    *   **Ack SeqNum 超前：** 這表示 Client Ack 了一個大於 DB 紀錄的數字。此時 DM 會在迴圈中強制 polling `response_ring_` 直到 empty (試圖將 DB 狀態追上)。若追平後 DB 的 SeqNum 仍落後 Client 的 Ack，代表 Client 狀態異常，直接回傳 `Logon Reject` 拒絕登入。

---

## 4. 🚨 待解決核心問題：`msg_seq_num` Race Condition 

由於您期望**方案 A**（CM 為了最低延遲，依舊自己讀取 `response_ring_` 並立即發送 Execution），這會產生一個嚴重的 Race Condition 導致 CM 與 DM 的 `msg_seq_num` 永遠脫鉤。

### 發生情境：
1.  Client 斷線重連，送出 Logon，CM 轉發給 DM。此時 Client Session 在 CM 還是 `is_ready = false`。
2.  Matching Engine 產生了一筆 Execution (假設 Index 100) 放入 `response_ring_`。
3.  **CM 處理較快**：CM 讀到 Index 100，因為 Client 尚未 Ready，CM 直接**丟棄**，且沒有遞增 SeqNum。
4.  **DM 處理較慢**：DM 處理了 Logon，查 DB 目前 SeqNum=5，回傳 `Logon Accept(OSeqNum=5)`。然後 DM 讀到 Index 100，將其寫入 DB，DB 中的 SeqNum 變為 **6**。
5.  CM 收到 `Logon Accept`，開始用 SeqNum=5 服務 Client。下一次 Execution 來臨時，CM 送出 **SeqNum 6**，但 DM 記成 **SeqNum 7**。
👉 **結果：兩邊的 SeqNum 永遠差 1，且 Client 永遠不知道自己漏了 Index 100 的資料！**

### 可考慮的解決方案 (待決定)：
*   **解法 1：由 Matching Engine (ME) 負責 SeqNum**
    讓 ME 產生的 `OrderResponseT` 直接自帶 SeqNum。這樣 CM 和 DM 只要讀取就好，不需要各自維護 counter。
*   **解法 2：CM 維護全局 SeqNum，兩邊各自對 RingBuffer 事件 +1 (CM is the source of truth)**
    CM 啟動時必須先載入**所有** Client 的 `OSeqNum` 到記憶體。之後 CM 只要從 `response_ring_` 讀到 Execution，**無論 Client 有沒有連線**，一律遞增該 Client 的 Memory SeqNum。DM 寫 DB 時也不查 DB 了，直接跟著 +1。
*   **解法 3：退回方案 B (由 DM 負責發送 Execution)**
    放棄 CM 直接讀取 `response_ring_`。由 DM 統一把 `response_ring_` 加上 SeqNum 後，放入 `data_resp_queue_` 轉交給 CM 發送。這是架構最乾淨、沒有 Race Condition 的做法，代價是增加了一次 SHM IPC 的微小延遲。
