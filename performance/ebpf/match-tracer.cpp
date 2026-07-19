#include <iostream>
#include <vector>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sqlite3.h>
#include "match-tracer.skel.h"

static volatile bool keep_running = true;

static void sig_handler(int) {
    keep_running = false;
}

#include <linux/perf_event.h>
#include <sys/syscall.h>

static int perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                           int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

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

sqlite3 *db;
sqlite3_stmt *stmt;
int batch_count = 0;
const int BATCH_SIZE = 10000;

static int handle_event(void *ctx, void *data, size_t data_sz) {
    auto *ev = static_cast<const match_trace_event *>(data);
    
    sqlite3_bind_int64(stmt, 1, ev->exec_id);
    sqlite3_bind_int(stmt, 2, ev->event_type);
    sqlite3_bind_int(stmt, 3, ev->is_start);
    sqlite3_bind_int64(stmt, 4, ev->ts);
    if (ev->map_name[0] != '\0') {
        sqlite3_bind_text(stmt, 5, ev->map_name, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    sqlite3_bind_int(stmt, 6, ev->pmu_l1);
    sqlite3_bind_int(stmt, 7, ev->pmu_llc);
    sqlite3_bind_int(stmt, 8, ev->pmu_branch);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert event\n";
    }
    sqlite3_reset(stmt);

    batch_count++;
    if (batch_count >= BATCH_SIZE) {
        sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
        batch_count = 0;
    }
    
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (sqlite3_open("traces.db", &db)) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    sqlite3_exec(db, "PRAGMA synchronous = OFF;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA journal_mode = MEMORY;", 0, 0, 0);
    sqlite3_exec(db, "DROP TABLE IF EXISTS events;", 0, 0, 0);
    sqlite3_exec(db, "CREATE TABLE events (exec_id INTEGER, event_type INTEGER, is_start INTEGER, ts INTEGER, map_name TEXT, pmu_l1 INTEGER, pmu_llc INTEGER, pmu_branch INTEGER);", 0, 0, 0);
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);

    const char *sql = "INSERT INTO events (exec_id, event_type, is_start, ts, map_name, pmu_l1, pmu_llc, pmu_branch) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement\n";
        return 1;
    }

    struct match_tracer_bpf *skel = match_tracer_bpf__open();
    if (!skel) return 1;

    if (match_tracer_bpf__load(skel)) {
        std::cerr << "Failed to load BPF skeleton\n";
        return 1;
    }

    int num_cpus = libbpf_num_possible_cpus();
    for (int cpu = 0; cpu < num_cpus; cpu++) {
        struct perf_event_attr attr = {};
        attr.size = sizeof(attr);
        
        attr.type = PERF_TYPE_HW_CACHE;
        attr.config = (PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
        int fd_l1 = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_l1 >= 0) {
            bpf_map_update_elem(bpf_map__fd(skel->maps.l1_miss_map), &cpu, &fd_l1, BPF_ANY);
        }

        attr.type = PERF_TYPE_HARDWARE;
        attr.config = PERF_COUNT_HW_CACHE_MISSES;
        int fd_llc = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_llc >= 0) {
            bpf_map_update_elem(bpf_map__fd(skel->maps.llc_miss_map), &cpu, &fd_llc, BPF_ANY);
        }

        attr.config = PERF_COUNT_HW_BRANCH_MISSES;
        int fd_branch = perf_event_open(&attr, -1, cpu, -1, 0);
        if (fd_branch >= 0) {
            bpf_map_update_elem(bpf_map__fd(skel->maps.branch_miss_map), &cpu, &fd_branch, BPF_ANY);
        }
    }

    const char *binary_path = "./build/services/matching-engine";
    long pid = -1;

#define ATTACH_USDT(skel_link, skel_prog, provider, probe_name) \
    skel->links.skel_link = bpf_program__attach_usdt(skel->progs.skel_prog, pid, binary_path, provider, probe_name, NULL); \
    if (!skel->links.skel_link) std::cerr << "Failed to attach USDT probe " << probe_name << "\n";

    ATTACH_USDT(trace_req_entry, trace_req_entry, "exchange", "ob_req_entry");
    ATTACH_USDT(trace_req_exit, trace_req_exit, "exchange", "ob_req_exit");
    ATTACH_USDT(trace_match_start, trace_match_start, "exchange", "ob_match_start");
    ATTACH_USDT(trace_match_end, trace_match_end, "exchange", "ob_match_end");
    ATTACH_USDT(trace_match_outer_start, trace_match_outer_start, "exchange", "ob_match_outer_start");
    ATTACH_USDT(trace_match_outer_end, trace_match_outer_end, "exchange", "ob_match_outer_end");
    ATTACH_USDT(trace_match_inner_start, trace_match_inner_start, "exchange", "ob_match_inner_start");
    ATTACH_USDT(trace_match_inner_end, trace_match_inner_end, "exchange", "ob_match_inner_end");
    ATTACH_USDT(trace_map_find_start, trace_map_find_start, "exchange", "ob_map_find_start");
    ATTACH_USDT(trace_map_find_end, trace_map_find_end, "exchange", "ob_map_find_end");
    ATTACH_USDT(trace_map_insert_start, trace_map_insert_start, "exchange", "ob_map_insert_start");
    ATTACH_USDT(trace_map_insert_end, trace_map_insert_end, "exchange", "ob_map_insert_end");
    ATTACH_USDT(trace_map_erase_start, trace_map_erase_start, "exchange", "ob_map_erase_start");
    ATTACH_USDT(trace_map_erase_end, trace_map_erase_end, "exchange", "ob_map_erase_end");
    ATTACH_USDT(trace_cancel_start, trace_cancel_start, "exchange", "ob_cancel_start");
    ATTACH_USDT(trace_cancel_end, trace_cancel_end, "exchange", "ob_cancel_end");
    ATTACH_USDT(trace_modify_start, trace_modify_start, "exchange", "ob_modify_start");
    ATTACH_USDT(trace_modify_end, trace_modify_end, "exchange", "ob_modify_end");
    ATTACH_USDT(trace_new_start, trace_new_start, "exchange", "ob_new_start");
    ATTACH_USDT(trace_new_end, trace_new_end, "exchange", "ob_new_end");
    ATTACH_USDT(trace_resp_reserve_start, trace_resp_reserve_start, "exchange", "ob_resp_reserve_start");
    ATTACH_USDT(trace_resp_new_start, trace_resp_new_start, "exchange", "ob_resp_new_start");
    ATTACH_USDT(trace_resp_commit_start, trace_resp_commit_start, "exchange", "ob_resp_commit_start");
    ATTACH_USDT(trace_resp_enqueue, trace_resp_enqueue, "exchange", "ob_resp_enqueue");

    skel->links.trace_sched_switch = bpf_program__attach(skel->progs.trace_sched_switch);
    if (!skel->links.trace_sched_switch) {
        std::cerr << "Failed to attach trace_sched_switch\n";
    }

    skel->links.trace_page_fault_entry = bpf_program__attach(skel->progs.trace_page_fault_entry);
    if (!skel->links.trace_page_fault_entry) std::cerr << "Failed to attach trace_page_fault_entry\n";
    
    skel->links.trace_page_fault_exit = bpf_program__attach(skel->progs.trace_page_fault_exit);
    if (!skel->links.trace_page_fault_exit) std::cerr << "Failed to attach trace_page_fault_exit\n";

    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) return 1;

    std::cout << "Tracing matching engine. Press Ctrl-C to stop..." << std::endl;

    while (keep_running) {
        ring_buffer__poll(rb, 100);
    }

    ring_buffer__free(rb);
    match_tracer_bpf__destroy(skel);

    sqlite3_exec(db, "COMMIT;", 0, 0, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    std::cout << "Trace saved to traces.db\n";
    return 0;
}
