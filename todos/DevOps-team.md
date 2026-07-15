# DevOps Team Strategic Roadmap & Improvement Plan

基於前人留下的 `devops_assessment.md`，目前專案的 Docker 化、CI/CD 部署、Nginx 反向代理以及零停機部署 (Zero-downtime Deployment) 已具備相當穩健的基礎。

作為具有 20 年經驗的 Senior DevOps Engineer，針對這個**微秒級高頻交易撮合引擎**，我認為我們已經度過了「求有」的階段，現在必須邁向「求精」與「企業級高可用性 (Enterprise HA)」。

以下是我為 DevOps 團隊規劃的待改進與新增功能藍圖：

## 1. 進階可觀測性與集中化日誌 (Advanced Observability & Logging)
目前依賴 Docker 標準輸出是不夠的，當系統異常時難以跨元件追蹤。
*   **集中化日誌系統 (Centralized Logging)**: 導入 **Grafana Loki** 或 **ELK (Elasticsearch, Logstash, Kibana)** Stack。設定 Docker Daemon 將日誌結構化為 JSON 格式並傳送至集中庫，並實作 Log Rotation 避免磁碟爆滿。
*   **營運指標監控 (Metrics & APM)**: 
    *   導入 **Prometheus** 與 **Grafana**。
    *   開發 Exporter 將 C++ 端的 `lat-tracer` (eBPF) 採集的微秒級延遲數據、CPU Cache Misses、訂單撮合吞吐量打入 Prometheus，實現延遲的**即時視覺化監控與告警 (Alerting)**。
*   **分散式追蹤 (Distributed Tracing)**: 針對跨微服務（Client Manager -> Matching Engine -> Mmap -> Database）的請求，評估導入 OpenTelemetry 確保單一訂單的生命週期完全透明。

## 2. 基礎設施即程式碼 (Infrastructure as Code - IaC)
目前是透過 GitHub Actions 直接 SSH 進 GCP VM 部署，這在擴展或災難重建時會面臨困難。
*   **Terraform / Pulumi 導入**: 將所有 GCP 資源（Compute Engine 實例、VPC 網路架構、防火牆規則、Cloud Storage Bucket）全部以 Terraform 程式碼化，確保環境的「可重複性 (Reproducibility)」。
*   **Ansible 配置管理**: 將 `scripts/system-tuning.sh` (CPU governor, THP, RT sched 等 OS 核心優化) 改寫為 Ansible Playbook，確保每一次開拓新機器時，底層作業系統的效能調優都是完全標準化的。

## 3. 效能與延遲防退化 CI 管道 (Performance Regression Pipeline)
（這與 QA 團隊的目標高度一致，但 DevOps 負責基礎設施的建置）
*   **Bare-metal CI Runners**: 啟用 `benchmark.yml.disabled`，但**絕不能**跑在一般 Cloud VM 上，因為 Noisy Neighbor 會造成 CPU Steal Time 波動，導致測試結果不準。需配置專屬的 Bare-metal Runner (實體機) 來跑效能測試。
*   **自動化效能閘門 (Performance Gating)**: 讓 CI 擁有決定權，當 PR 的延遲數據退化超過基線，自動拒絕 Merge。

## 4. 資安與機密管理 (Security & Secrets Management)
金融交易系統對資安的要求極高，不能只靠 GitHub Secrets。
*   **動態機密管理**: 導入 **Google Secret Manager** 或 **HashiCorp Vault**。將資料庫密碼、TLS 憑證與 API 金鑰移出 `.env` 檔案，改為服務啟動時動態向 Vault 請求，並設定密鑰定期輪替 (Secret Rotation)。
*   **自動化資安掃描 (DevSecOps)**: 
    *   在 CI 流程中加入 **Trivy** 或 **Snyk**，針對構建出來的 Docker Images 進行 CVE 弱點掃描。
    *   使用 SAST (靜態應用程式安全測試) 掃描 C++ 與 TypeScript 程式碼。

## 5. 災難復原與高可用性 (Disaster Recovery & HA)
目前依賴單一 GCP VM，這是一個單點故障 (SPOF, Single Point of Failure)。
*   **自動化異地備份 (Automated Backups)**: 撰寫 CronJob 或 Kubernetes CronJob，定期將資料庫（SQLite/Postgres）與 Mmap 執行日誌打包備份至 GCP Cloud Storage，並套用生命週期策略（例如 30 天後轉存冷資料）。
*   **Hot/Cold 備援架構**: 在另一個 GCP Availability Zone 建置 Cold Standby 環境。結合 IaC，當主機房發生災難時，能在 5 分鐘內透過 DNS 切換與 Terraform 重建環境並接手流量。
*   **Kubernetes 遷移評估 (K8s Migration)**:
    雖然 Docker Compose + Host Network 對低延遲很棒，但不利於大規模叢集管理。評估遷移至 Google Kubernetes Engine (GKE)，並透過 `Multus CNI` 或 `HostNetwork` 加上 `NodeAffinity`，在享受 K8s 自癒能力與彈性擴縮容的同時，依然保有極致的網路效能與 IPC 共享記憶體 (`/dev/shm`)。

## 6. 資料庫的高效能演進 (Database Evolution)
*   隨著交易量上升，當前的 SQLite/CSV 或單節點 PostgreSQL 遲早會成為瓶頸。規劃從單節點資料庫無縫遷移至高可用的分散式資料庫架構（如 Cloud SQL for PostgreSQL 搭配 Read Replicas，或 TimescaleDB 以處理時間序列的市場數據）。
