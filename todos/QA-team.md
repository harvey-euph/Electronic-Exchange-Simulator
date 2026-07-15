# QA Team Strategic Roadmap & Improvement Plan

As a Senior QA Engineer with 20 years of experience, this document outlines the comprehensive quality assurance, testing optimization, and new feature plans for our high-performance C++ exchange ecosystem.

## 1. 測試自動化與 CI/CD 流程優化 (Test Automation & CI/CD Pipeline)
- **CI/CD Pipeline Revamp**: 
  - 在 GitHub Actions / Gitlab CI 中全面啟用 Automated Testing。
  - 確保每一次 PR 都能在乾淨的容器或環境中完成編譯與所有 GTest 單元測試。
- **導入 Sanitizers (動態分析)**:
  - 修改 `Makefile` 以支援 ASan (AddressSanitizer), TSan (ThreadSanitizer), UBSan (UndefinedBehaviorSanitizer)。
  - 針對 Lock-free SHM Ring Buffer 與 Mmap 進行嚴格的併發與記憶體外洩監測。
- **靜態程式碼分析 (Static Analysis)**:
  - 整合 `SonarQube` 或 `clang-tidy`，強制執行 Modern C++ 的最佳實踐與安全規範，從源頭阻絕潛在邏輯錯誤。
- **測試覆蓋率與報告 (Code Coverage & Reporting)**:
  - 導入 `gcov/lcov` 生成覆蓋率報告。
  - 將核心的撮合邏輯 (Matching Engine Logic) 覆蓋率閥值設定為 90% 以上。
  - 自動生成 JUnit / Allure 報表，提供視覺化的測試結果。

## 2. 效能與延遲防退化測試 (Performance & Latency Regression)
- **Bare-metal Benchmark Runner**:
  - 修復並重啟目前被停用的 `benchmark.yml.disabled`。
  - 將效能測試部署於 Bare-metal 實體機，避免 Cloud VM 的 CPU Steal Time 導致延遲測量誤差。
- **Latency Baseline 監測**:
  - 利用專案中的 eBPF `lat-tracer` 追蹤各階段處理延遲。
  - 若新 PR 的 P99 延遲或 CPU Cache Misses 較 Main 分支退化超過特定閾值（如 > 5 微秒），自動 Block 該 PR 合併。
- **Nightly Soak Test (長時間浸泡測試)**:
  - 解除 `scripts/test-correctness` 中對 `stress*` 資料夾的忽略限制。
  - 每晚啟動 `benchmark-trader` 與 `chaos-stress-testers`，連續高頻打單 8 小時。
  - 隔日清晨檢驗系統的 Throughput 是否衰退，以及是否存在 Memory Leak 跡象。

## 3. 進階測試技術導入 (Advanced Testing Techniques)
- **Fuzz Testing (模糊測試)**:
  - 針對 `ClientManager` 接收 WebSocket FlatBuffers 的入口導入 `libFuzzer` 或 `AFL++`。
  - 確保引擎面臨畸形封包或惡意 Payload 時不會 Crash，保障金融系統的極限可用性。
- **Property-Based Testing (屬性測試)**:
  - 導入 `RapidCheck` (C++)，隨機生成上萬筆合法訂單序列。
  - 自動驗證系統不變量 (Invariants)：例如「Ask 最低價永遠必須大於 Bid 最高價 (無 Crossed Orderbook)」、「所有委託單殘量加總加上歷史成交量必須等於原始下單總量」等。
- **API 契約測試 (Contract Testing) [新增]**:
  - 針對 WebSocket 與 HTTP 介面導入契約測試（如 Pact）。
  - 確保前後端資料格式 (FlatBuffers) 在疊代過程中，不會因為欄位增減破壞向後相容性，導致 Client 端拋錯。

## 4. 混沌工程與容錯測試 (Chaos Engineering & Fault Tolerance)
- **Zero Data Loss 驗證**:
  - 在高頻撮合進行中，隨機中止 (SIGKILL) `Matching Engine` 或 `Client Manager` 行程。
  - 驗證系統重啟後，能否從 Mmap 執行日誌與 SHM Ring Buffer 中 100% 完美重建 Orderbook 狀態，實現零資料遺失。
- **Network Chaos**:
  - 使用 Linux `tc` (Traffic Control) 模擬真實網路環境的封包延遲、遺失與斷線重連。
  - 驗證 WebSocket 串流是否能正確觸發 `Empty Frame` 與 `Snapshot` 同步機制，並確保 Client 端 Orderbook 狀態最終一致。
- **資料庫背景同步壓力測試 [新增]**:
  - 測試 Database Background Polling 從 Mmap 執行日誌寫回實體資料庫時的容錯度。
  - 模擬資料庫連線中斷或 I/O 寫入極慢的情境，驗證系統的 Backpressure (背壓) 機制與非同步解耦是否確實生效，不會拖慢 Matching Engine。

## 5. 測試框架重構與前端測試 (Framework Refactoring & UI E2E)
- **Python (PyTest) 取代 Bash E2E 腳本**:
  - 將目前的 `scripts/test-correctness` bash 腳本完全遷移至 `PyTest`。
  - 利用 PyTest 豐富的生態系進行平行測試、Data-driven (參數化測試)，並且能大幅簡化複雜的斷言邏輯 (Assertions)。
- **Frontend Playwright / Cypress E2E**:
  - 撰寫瀏覽器端自動化測試，模擬使用者行為。
  - 特別驗證 React UI 在面臨每秒上千筆 Orderbook L2/L3 更新時，資料 Throttling (節流) 機制與渲染效能是否能保持畫面流暢不卡頓。
- **Visual Regression Testing (視覺防退化) [新增]**:
  - 整合截圖比對工具，確保 CSS/UI 元件更新不會造成前端金融儀表板的價格數字閃爍或版面異常跑位。

## 6. 自動化基礎環境 (Test Environment Provisioning) [新增]
- **Ephemeral Environments (拋棄式測試環境)**:
  - 結合目前的 Docker Compose 與 CI Pipeline，讓每一個 PR 都能自動 Spin-up 一整套完全隔離的 Exchange 測試環境（包含 DB, Backend Server, UI）。
  - 讓 QA、PM 與開發者都能夠過一鍵生成的臨時網址，立即驗證 PR 的功能與效能，加速開發反饋循環。
