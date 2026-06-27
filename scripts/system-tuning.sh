#!/bin/bash

apply_system_tuning() {
    echo "Applying low-latency system tuning..."

    # CPU governor
    if command -v cpupower >/dev/null 2>&1; then
        cpupower frequency-set -g performance >/dev/null 2>&1
    fi

    for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [ -f "$gov" ] && echo performance > "$gov" 2>/dev/null
    done

    # Disable Transparent Huge Page
    if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then
        echo never > /sys/kernel/mm/transparent_hugepage/enabled
    fi

    if [ -f /sys/kernel/mm/transparent_hugepage/defrag ]; then
        echo never > /sys/kernel/mm/transparent_hugepage/defrag
    fi

    # Disable swap
    swapoff -a 2>/dev/null

    # RT scheduler
    sysctl -w kernel.sched_rt_runtime_us=-1 >/dev/null

    # Memory behavior
    sysctl -w vm.swappiness=0 >/dev/null
    sysctl -w vm.zone_reclaim_mode=0 >/dev/null

    # Increase file descriptor limit
    ulimit -n 1048576
    ulimit -l unlimited

    # Boost ksoftirqd (network softirq) priority
    echo "Boosting network kernel threads (ksoftirqd) priority..."
    for pid in $(pgrep ksoftirqd); do
        chrt -f -p 90 $pid 2>/dev/null || true
    done

    echo "System tuning applied."
}

restore_system_tuning() {
    echo "Restoring system tuning to default..."

    # 1. 恢復 CPU 調速器為動態節能（一般伺服器預設為 powersave 或 schedutil）
    if command -v cpupower >/dev/null 2>&1; then
        cpupower frequency-set -g powersave >/dev/null 2>&1
    fi
    for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [ -f "$gov" ] && echo powersave > "$gov" 2>/dev/null
    done

    # 2. 恢復 Transparent Huge Page 為預設的啟用狀態
    if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then
        echo always > /sys/kernel/mm/transparent_hugepage/enabled
    fi
    if [ -f /sys/kernel/mm/transparent_hugepage/defrag ]; then
        echo madvise > /sys/kernel/mm/transparent_hugepage/defrag
    fi

    # 3. 重新啟用 Swap
    swapon -a 2>/dev/null

    # 4. 恢復 RT scheduler 限制
    sysctl -w kernel.sched_rt_runtime_us=950000 >/dev/null

    # 5. 恢復 記憶體行為 預設值
    sysctl -w vm.swappiness=60 >/dev/null
    sysctl -w vm.zone_reclaim_mode=0 >/dev/null

    echo "System tuning restored to default."
}
