#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-declarations"
#include "vmlinux.h"
#pragma clang diagnostic pop

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

char LICENSE[] SEC("license") = "GPL";

struct match_trace_event {
    uint64_t exec_id;
    uint32_t event_type;
    uint32_t is_start;
    uint64_t ts;
    char map_name[16];
    uint32_t pmu_l1;
    uint32_t pmu_llc;
    uint32_t pmu_branch;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, uint32_t);
    __type(value, uint64_t);
} active_thread_exec_id SEC(".maps");
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} l1_miss_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} llc_miss_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(int));
    __uint(value_size, sizeof(int));
} branch_miss_map SEC(".maps");

static __always_inline void read_pmu(struct match_trace_event *e) {
    long l1 = bpf_perf_event_read(&l1_miss_map, BPF_F_CURRENT_CPU);
    long llc = bpf_perf_event_read(&llc_miss_map, BPF_F_CURRENT_CPU);
    long br = bpf_perf_event_read(&branch_miss_map, BPF_F_CURRENT_CPU);
    e->pmu_l1 = (l1 > 0) ? (uint32_t)l1 : 0;
    e->pmu_llc = (llc > 0) ? (uint32_t)llc : 0;
    e->pmu_branch = (br > 0) ? (uint32_t)br : 0;
}

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 262144 * 16); // 4MB
} rb SEC(".maps");

static __always_inline void record_event(uint32_t event_type, uint32_t is_start, const char *map_name_ptr) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (exec_id_ptr) {
        struct match_trace_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->exec_id = *exec_id_ptr;
            e->event_type = event_type;
            e->is_start = is_start;
            e->ts = bpf_ktime_get_ns();
            e->ts = bpf_ktime_get_ns();
            long l1 = bpf_perf_event_read(&l1_miss_map, BPF_F_CURRENT_CPU);
            long llc = bpf_perf_event_read(&llc_miss_map, BPF_F_CURRENT_CPU);
            long br = bpf_perf_event_read(&branch_miss_map, BPF_F_CURRENT_CPU);
            e->pmu_l1 = (l1 > 0) ? (uint32_t)l1 : 0;
            e->pmu_llc = (llc > 0) ? (uint32_t)llc : 0;
            e->pmu_branch = (br > 0) ? (uint32_t)br : 0;
            if (map_name_ptr) {
                bpf_probe_read_user_str(e->map_name, sizeof(e->map_name), map_name_ptr);
            } else {
                e->map_name[0] = '\0';
            }
            bpf_ringbuf_submit(e, 0);
        }
    }
}

SEC("usdt")
int trace_req_entry(struct pt_regs *ctx) {
    uint64_t exec_id = 0;
    bpf_usdt_arg(ctx, 0, (long *)&exec_id);
    uint32_t tid = bpf_get_current_pid_tgid();
    bpf_map_update_elem(&active_thread_exec_id, &tid, &exec_id, BPF_ANY);
    record_event(0, 1, NULL);
    return 0;
}

SEC("usdt")
int trace_req_exit(struct pt_regs *ctx) {
    record_event(0, 0, NULL);
    uint32_t tid = bpf_get_current_pid_tgid();
    bpf_map_delete_elem(&active_thread_exec_id, &tid);
    return 0;
}

#define TRACE_PROBE(name, id) \
SEC("usdt") \
int trace_##name##_start(struct pt_regs *ctx) { record_event(id, 1, NULL); return 0; } \
SEC("usdt") \
int trace_##name##_end(struct pt_regs *ctx) { record_event(id, 0, NULL); return 0; }

TRACE_PROBE(match, 1)
TRACE_PROBE(match_outer, 2)
TRACE_PROBE(match_inner, 3)

SEC("usdt")
int trace_map_find_start(struct pt_regs *ctx) {
    long map_name_ptr = 0;
    bpf_usdt_arg(ctx, 0, &map_name_ptr);
    record_event(4, 1, (const char *)map_name_ptr);
    return 0;
}
SEC("usdt")
int trace_map_find_end(struct pt_regs *ctx) { record_event(4, 0, NULL); return 0; }
SEC("usdt")
int trace_map_insert_start(struct pt_regs *ctx) {
    long map_name_ptr = 0;
    bpf_usdt_arg(ctx, 0, &map_name_ptr);
    record_event(5, 1, (const char *)map_name_ptr);
    return 0;
}
SEC("usdt")
int trace_map_insert_end(struct pt_regs *ctx) { record_event(5, 0, NULL); return 0; }

SEC("usdt")
int trace_map_erase_start(struct pt_regs *ctx) {
    long map_name_ptr = 0;
    bpf_usdt_arg(ctx, 0, &map_name_ptr);
    record_event(6, 1, (const char *)map_name_ptr);
    return 0;
}
SEC("usdt")
int trace_map_erase_end(struct pt_regs *ctx) { record_event(6, 0, NULL); return 0; }
TRACE_PROBE(cancel, 7)
TRACE_PROBE(modify, 8)
TRACE_PROBE(new, 9)

SEC("usdt")
int trace_create_order_start(struct pt_regs *ctx) { record_event(17, 1, NULL); return 0; }
SEC("usdt")
int trace_create_order_end(struct pt_regs *ctx) { record_event(17, 0, NULL); return 0; }

SEC("usdt")
int trace_resp_reserve_start(struct pt_regs *ctx) { record_event(11, 1, NULL); return 0; }
SEC("usdt")
int trace_resp_new_start(struct pt_regs *ctx) { record_event(12, 1, NULL); return 0; }
SEC("usdt")
int trace_resp_commit_start(struct pt_regs *ctx) { record_event(13, 1, NULL); return 0; }

SEC("usdt")
int trace_resp_enqueue(struct pt_regs *ctx) {
    record_event(10, 1, NULL);
    return 0;
}

SEC("tp_btf/sched_switch")
int BPF_PROG(trace_sched_switch, bool preempt, struct task_struct *prev, struct task_struct *next) {
    uint32_t prev_tid = prev->pid;
    uint32_t next_tid = next->pid;

    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &prev_tid);
    if (exec_id_ptr) {
        struct match_trace_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->event_type = 14; 
            e->is_start = 1;
            e->ts = bpf_ktime_get_ns();
            e->exec_id = *exec_id_ptr;
            e->map_name[0] = '\0';
            read_pmu(e);
            bpf_ringbuf_submit(e, 0);
        }
    }

    exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &next_tid);
    if (exec_id_ptr) {
        struct match_trace_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (e) {
            e->event_type = 15;
            e->is_start = 1;
            e->ts = bpf_ktime_get_ns();
            e->exec_id = *exec_id_ptr;
            e->map_name[0] = '\0';
            read_pmu(e);
            bpf_ringbuf_submit(e, 0);
        }
    }
    return 0;
}

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(trace_page_fault_entry) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (!exec_id_ptr) return 0;
    
    struct match_trace_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) return 0;
    
    e->event_type = 16;
    e->is_start = 1;
    e->ts = bpf_ktime_get_ns();
    e->exec_id = *exec_id_ptr;
    e->map_name[0] = '\0';
    read_pmu(e);
    
    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("kretprobe/handle_mm_fault")
int BPF_KRETPROBE(trace_page_fault_exit) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (!exec_id_ptr) return 0;
    
    struct match_trace_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) return 0;
    
    e->event_type = 16;
    e->is_start = 0;
    e->ts = bpf_ktime_get_ns();
    e->exec_id = *exec_id_ptr;
    e->map_name[0] = '\0';
    read_pmu(e);
    
    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tracepoint/irq/irq_handler_entry")
int trace_irq_entry(void *ctx) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (exec_id_ptr) record_event(18, 1, "hard_irq");
    return 0;
}

SEC("tracepoint/irq/irq_handler_exit")
int trace_irq_exit(void *ctx) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (exec_id_ptr) record_event(18, 0, "hard_irq");
    return 0;
}

SEC("tracepoint/irq/softirq_entry")
int trace_softirq_entry(void *ctx) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (exec_id_ptr) record_event(19, 1, "softirq");
    return 0;
}

SEC("tracepoint/irq/softirq_exit")
int trace_softirq_exit(void *ctx) {
    uint32_t tid = bpf_get_current_pid_tgid();
    uint64_t *exec_id_ptr = bpf_map_lookup_elem(&active_thread_exec_id, &tid);
    if (exec_id_ptr) record_event(19, 0, "softirq");
    return 0;
}
