# OS Jitter 與核心搶占分析 (OS Jitter & Kernel Preemption Analysis)

在量化交易或高效能撮合引擎（Matching Engine）的開發中，我們追求微秒（us）甚至奈秒（ns）等級的極致延遲。在這個層級中，作業系統本身的背景活動常常成為最大的效能殺手，這被稱為 **OS Jitter（作業系統抖動）**。

## 事件回顧：10 毫秒的延遲突波

在一次 eBPF (`match-tracer`) 的追蹤分析中，我們觀測到一個撮合請求的延遲出現了高達 **10 毫秒 (10,236 us)** 的突波。

透過追蹤樹狀圖 (Trace Tree Viewer)，我們發現 C++ 程式碼層面完全正常：從 `handleModify started` 到撮合完成依然只花了不到 120 us。然而，在進入 `processRequest started` 之後，執行緒竟然經歷了上百次的 **Context Switch**！

```text
--- Trace Tree Viewer ---
Run 12 of 14 (exec_id: 1784490854132)
--------------------------------------------------
     0.000 us -> processRequest started
    32.100 us   === sched_out (context switched out) ===
    77.900 us   === sched_in (context switched in) ===
    95.700 us   === sched_out (context switched out) ===
   110.600 us   === sched_in (context switched in) ===
... (共發生上百次切換) ...
 10066.700 us   === sched_in (context switched in) ===
 10119.000 us   -> handleModify started
```

## 根本原因：中斷風暴 (Interrupt Storm) 與「隱形」的 CPU 徵用

這 10 毫秒的突發延遲是由於背景 OS 發生了突發性的大量活動（例如網路封包湧入、磁碟 I/O 或是 WSL2 的 Hypervisor 同步），導致硬體中斷 (Hard IRQ) 不斷發生。

### 為什麼看似執行了 12us，程式碼卻沒前進？
你可能會疑惑：在兩次 Context Switch 之間，執行緒明明處於 `sched_in` 狀態長達十幾微秒，這段時間足夠 CPU 執行上萬行指令了，為什麼連一個簡單的 `switch` 敘述都跑不完？

答案是：**這十幾微秒內，CPU 根本不在執行 C++ 程式碼！**

eBPF 的 `sched_switch` 探針追蹤的是作業系統排程器 (Scheduler) 的任務切換。然而，**硬體中斷並不會觸發 `sched_switch`**。其真實的運作劇本如下：
1. 程式剛印出 `processRequest started`，此時執行緒屬於 `sched_in` (擁有 CPU)。
2. 網路卡發出硬體中斷，CPU 立刻暫停 User-space 的 C++ 程式，強制進入 Kernel-space 執行硬體中斷處理程序。
3. 排程器名冊上，你的執行緒依然「擁有」CPU，因此你看到了所謂的「執行了 12us」，但實際上這 12us 全部被 Kernel 拿去處理封包了。
4. 硬體中斷處理完畢後，發現積壓了太多工作，於是喚醒了專職處理軟中斷的核心執行緒（如 `ksoftirqd`）。
5. 喚醒 `ksoftirqd` 觸發了排程，你的程式正式被 **`sched_out`**，換 `ksoftirqd` 執行約 20us。
6. `ksoftirqd` 完工後交出 CPU，你的程式被 **`sched_in`**。
7. 你的程式正準備跑下一行 C++ 指令，**下一個硬體中斷又來了**。
8. 上述過程在 10 毫秒內瘋狂重複了上百次。

這證明了：即便程式碼寫得再快，只要 CPU 未能徹底隔離，系統排程干擾 (OS Jitter) 以及隱形的硬體中斷就會徹底破壞尾部延遲 (Tail Latency)。

## 解決方案：如何達成 Zero OS Jitter？

要在生產環境（Server 機器）上避免這個問題，必須將專門運行撮合引擎的 CPU 核心從 Linux 排程器與中斷處理中徹底剝離。

### 1. 修改 GRUB 開機參數隔離 CPU
在 `/etc/default/grub` 中加入 `isolcpus` 與 `nohz_full` 等參數，告訴 Linux 核心永遠不要將一般任務或 Timer Interrupt 丟到這些核心上：
```bash
GRUB_CMDLINE_LINUX_DEFAULT="... isolcpus=4 nohz_full=4 rcu_nocbs=4"
```
*(設定完成後需執行 `update-grub` 並重開機)*

### 2. 轉移硬體中斷 (IRQ Affinity)
即使使用了 `isolcpus`，硬體中斷（如網卡、NVMe）仍可能預設派發給所有的核心。必須將中斷路由轉移到其他核心（例如 Core 0-3）：
- **停用 irqbalance 服務**：
  ```bash
  sudo systemctl stop irqbalance
  sudo systemctl disable irqbalance
  ```
- **手動設定 SMP Affinity**：
  確保所有硬體中斷的 `/proc/irq/*/smp_affinity` 遮罩 (Mask) 都不包含 Core 4。

### 3. 停用機器省電與降頻機制
CPU 的電源狀態切換也會導致延遲抖動：
- 進入 BIOS 關閉 **C-States** 與 **Intel SpeedStep / P-States**。
- 在系統中將 CPU Governor 設為 performance：
  ```bash
  cpupower frequency-set -g performance
  ```

### 4. 開發/測試環境 (如 WSL2) 的認知
- 在 WSL2 或 Docker 等高度虛擬化的本機開發環境中，上述的實體核心隔離無法完美實現。
- 測試數據若出現偶發的極高延遲 (P99.99 突波)，我們應透過類似 `match-tracer` (包含 `sched_switch` 探針) 的工具來釐清「是程式慢，還是 CPU 被搶走」。只要確認為 OS Jitter，便能確定在調校良好的 Server 上不會發生。
