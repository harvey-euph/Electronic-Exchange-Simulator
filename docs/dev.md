# API 開發者指南 (API Developer Guide)

本指南專為量化交易員、造市商 (Market Maker) 與演算法開發者編寫，詳細說明如何透過 WebSocket 連接我們的交易所、傳遞認證資訊、發送委託，並處理執行回報。

## 1. 連線與認證 (Connection & Auth)

本交易所完全透過 WebSocket (WS) 提供極低延遲的交易 API。交易網路層與應用層高度解耦，所有的 Client 連線都會對應到專屬的 Session 物件以確保高吞吐量的訊息隔離。

### 端點與協定
- **Client Manager 端點**：負責接受下單 (New, Modify, Cancel) 與私有帳戶回報 (Execution Reports)。
- **通訊格式**：目前支援 JSON 與二進位 Flatbuffer。
- **序列號 (Sequence Number)**：
  - 客戶端與伺服器之間維護嚴格的 `MsgSeqNum` 與 `AckSeqNum`。
  - **登入 (Login)** 時，客戶端必須夾帶預期的 SeqNum。如果發生斷線，客戶端重連時提供正確的 `AckSeqNum`，伺服器會自動把遺漏的 Execution Reports 補齊並重傳。如果客戶端的 SeqNum 發生不可修復的落後，伺服器會拒絕連線。

---

## 2. 請求格式 (Request Specification)

所有的下單請求都會被轉換為內部的 `OrderRequestT`。以下是核心操作：

### 2.1 新增委託 (New Order)
傳入 `Action = New`，指定：
- `client_id`：你的帳戶 ID。
- `order_id`：由 Client 端生成的唯一訂單編號。
- `symbol_id`：交易對 ID。
- `side`：Buy 或 Sell。
- `type`：Limit 或 Market。
- `p`：委託價格 (Market Order 填 0)。
- `q`：委託總量。

### 2.2 修改委託 (Modify Order)
傳入 `Action = Modify`，針對已掛單的 `order_id`：
- `p`：修改後的新價格。
- `q`：修改後的**目標總量 (New Target OrderQty)**。
> ⚠️ **注意**：修改委託時，請求中的 `q` 代表的是「這張單的總量希望變成多少」，而不是剩餘數量。系統會自動扣除已成交數量來推算新的剩餘量。

### 2.3 取消委託 (Cancel Order)
傳入 `Action = Cancel`，並帶上對應的 `order_id`。

---

## 3. 回報格式與生命週期 (Response & ExecType Semantics)

伺服器會將 Matching Engine 的撮合結果透過 `OrderResponseT` 推播給您。為追求極致的傳輸效能，我們將 `Price (p)` 與 `Quantity (q)` 在不同的 `ExecType` (執行狀態) 下賦予了不同的語意。

請**嚴格參考以下核心語意表**來實作您的本地記帳邏輯：

| ExecType (執行狀態) | `p` (Price) 的語意 | `q` (Quantity) 的語意 | 說明 (與 FIX Protocol 的對應) |
| :--- | :--- | :--- | :--- |
| **`New`** (新增成功) | **掛單價格**<br>*(Market order 則為 0)* | **委託總量**<br>(`qty_original`) | 確認收單，此時 `q` 同時等於 `OrderQty` 與 `LeavesQty`。 |
| **`Replaced`** (修改成功) | **修改後的新價格**<br>*(Target Price)* | **修改後的新剩餘量**<br>(`new_qty_remaining`) | **狀態覆蓋**。MD 與 DB 會直接將剩餘數量 (LeavesQty) 覆蓋為此數值。 |
| **`PartialFill`** (部分成交) | **本次成交價格**<br>*(Match Price)* | **本次成交數量**<br>(`match_qty`) | **交易事件**。代表在此價格下搓合了多少數量 (LastPx / LastQty)。 |
| **`Fill`** (完全成交) | **本次成交價格**<br>*(Match Price)* | **本次成交數量**<br>(`match_qty`) | 同上。這筆單完成搓合，剩餘數量歸零。 |
| **`Cancelled`** (取消成功) | **取消前的掛單價格** | **固定為 `0`** | 狀態覆蓋。直接告知該訂單已不存在。 |
| **`Rejected`** (拒絕請求) | **被拒絕的請求價格** | **被拒絕的請求數量** | 將 Client 原本送來的不合法參數原封不動退回，並附帶 Reject Code 供 Client 檢查。 |

### 設計亮點 (State Updates vs Transaction Events)
我們將回報巧妙分為兩大類：
1. **狀態更新類 (`New`, `Replaced`, `Cancelled`)**：
   - 此類回報的 `q` 永遠代表**實際剩餘總量 (LeavesQty)**。客戶端收到後應直接覆蓋本地狀態。
2. **交易事件類 (`PartialFill`, `Fill`)**：
   - 此類回報的 `q` 永遠代表**單筆成交量 (LastQty)**。這確保了演算法交易者絕對可以精確計算均價與成交總額。

---

## 4. 本地狀態維護建議 (Best Practices)

1. **維護本地 Order Book**：
   為了掌握市場全貌，建議同時連線至 **Market Data Server** 訂閱 `L2 Snapshot` 與 `Delta Updates`。當收到 L2 更新時，可直接替換/增減對應價位的總量。
2. **斷線重連機制**：
   如果斷線時間過長，或是 Sequence Number 落後太多被拒絕連線，建議您先發送 REST API 獲取 `Open Orders` 狀態進行本地對齊，再發起 WebSocket 建立新的 Session。
