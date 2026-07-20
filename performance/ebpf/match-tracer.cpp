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
#include <map>
#include <string>
#include <cxxabi.h>
#include <cstring>
#include <cstdio>

const int BATCH_SIZE = 10000;
std::map<uint64_t, std::string> cookie_to_name;

std::string demangle(const char* name) {
    int status = -4;
    char* res = abi::__cxa_demangle(name, NULL, NULL, &status);
    std::string ret = (status == 0) ? res : name;
    free(res);
    size_t pos = ret.find('(');
    if (pos != std::string::npos) ret = ret.substr(0, pos);
    pos = ret.find("Exchange::OrderBook::");
    if (pos != std::string::npos) ret = ret.substr(pos + 21);
    return ret;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    auto *ev = static_cast<const match_trace_event *>(data);
    
    sqlite3_bind_int64(stmt, 1, ev->exec_id);
    
    if (ev->event_type >= 1000 && ev->event_type < 10000) {
        std::string name = cookie_to_name[ev->event_type];
        if (!name.empty()) {
            sqlite3_bind_int(stmt, 2, 999);
            sqlite3_bind_text(stmt, 5, name.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_int(stmt, 2, ev->event_type);
            sqlite3_bind_text(stmt, 5, "MISSING_COOKIE", -1, SQLITE_TRANSIENT);
        }
    } else {
        sqlite3_bind_int(stmt, 2, ev->event_type);
        if (ev->map_name[0] != '\0') {
            sqlite3_bind_text(stmt, 5, ev->map_name, -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 5);
        }
    }
    sqlite3_bind_int(stmt, 3, ev->is_start);
    sqlite3_bind_int64(stmt, 4, ev->ts);
    
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
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS events (exec_id INTEGER, event_type INTEGER, is_start INTEGER, ts INTEGER, map_name TEXT, pmu_l1 INTEGER, pmu_llc INTEGER, pmu_branch INTEGER);", 0, 0, 0);
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

    std::vector<struct bpf_link*> generic_links;
    FILE* fp = popen("nm --defined-only ./build/services/matching-engine | grep _ZN8Exchange9OrderBook | awk '{print $3}'", "r");
    char line[256];
    uint64_t cookie = 1000;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        std::string mangled = line;
        std::string name = demangle(mangled.c_str());
        
        cookie_to_name[cookie] = name;
        
        LIBBPF_OPTS(bpf_uprobe_opts, opts);
        opts.func_name = mangled.c_str();
        opts.retprobe = false;
        opts.bpf_cookie = cookie;
        
        if (name == "processRequest") {
            skel->links.trace_processRequest_start = bpf_program__attach_uprobe_opts(skel->progs.trace_processRequest_start, pid, binary_path, 0, &opts);
            opts.retprobe = true;
            skel->links.trace_processRequest_end = bpf_program__attach_uprobe_opts(skel->progs.trace_processRequest_end, pid, binary_path, 0, &opts);
        } else {
            struct bpf_link* l1 = bpf_program__attach_uprobe_opts(skel->progs.trace_generic_entry, pid, binary_path, 0, &opts);
            if (l1) generic_links.push_back(l1);
            opts.retprobe = true;
            struct bpf_link* l2 = bpf_program__attach_uprobe_opts(skel->progs.trace_generic_exit, pid, binary_path, 0, &opts);
            if (l2) generic_links.push_back(l2);
        }
        cookie++;
    }
    pclose(fp);
    
    // special createOrder probe (since it's not in OrderBook namespace)
    LIBBPF_OPTS(bpf_uprobe_opts, opts_co);
    opts_co.func_name = "_Z11createOrderPKN8Exchange13OrderRequestTE";
    opts_co.bpf_cookie = cookie;
    cookie_to_name[cookie++] = "createOrder";
    opts_co.retprobe = false;
    struct bpf_link* co_start = bpf_program__attach_uprobe_opts(skel->progs.trace_generic_entry, pid, binary_path, 0, &opts_co);
    if (co_start) generic_links.push_back(co_start);
    opts_co.retprobe = true;
    struct bpf_link* co_end = bpf_program__attach_uprobe_opts(skel->progs.trace_generic_exit, pid, binary_path, 0, &opts_co);
    if (co_end) generic_links.push_back(co_end);

    skel->links.trace_sched_switch = bpf_program__attach(skel->progs.trace_sched_switch);
    if (!skel->links.trace_sched_switch) {
        std::cerr << "Failed to attach trace_sched_switch\n";
    }

    skel->links.trace_page_fault_entry = bpf_program__attach(skel->progs.trace_page_fault_entry);
    if (!skel->links.trace_page_fault_entry) std::cerr << "Failed to attach trace_page_fault_entry\n";
    
    skel->links.trace_page_fault_exit = bpf_program__attach(skel->progs.trace_page_fault_exit);
    if (!skel->links.trace_page_fault_exit) std::cerr << "Failed to attach trace_page_fault_exit\n";

    skel->links.trace_irq_entry = bpf_program__attach(skel->progs.trace_irq_entry);
    if (!skel->links.trace_irq_entry) std::cerr << "Failed to attach trace_irq_entry\n";
    skel->links.trace_irq_exit = bpf_program__attach(skel->progs.trace_irq_exit);
    if (!skel->links.trace_irq_exit) std::cerr << "Failed to attach trace_irq_exit\n";

    skel->links.trace_softirq_entry = bpf_program__attach(skel->progs.trace_softirq_entry);
    if (!skel->links.trace_softirq_entry) std::cerr << "Failed to attach trace_softirq_entry\n";
    skel->links.trace_softirq_exit = bpf_program__attach(skel->progs.trace_softirq_exit);
    if (!skel->links.trace_softirq_exit) std::cerr << "Failed to attach trace_softirq_exit\n";
    
    skel->links.trace_sys_enter = bpf_program__attach(skel->progs.trace_sys_enter);
    if (!skel->links.trace_sys_enter) std::cerr << "Failed to attach trace_sys_enter\n";
    skel->links.trace_sys_exit = bpf_program__attach(skel->progs.trace_sys_exit);
    if (!skel->links.trace_sys_exit) std::cerr << "Failed to attach trace_sys_exit\n";

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
