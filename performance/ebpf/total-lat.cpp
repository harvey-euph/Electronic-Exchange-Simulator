#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include "total-lat.skel.h"

static volatile bool keep_running = true;

static void sig_handler(int) {
    keep_running = false;
}

struct latency_event {
    uint8_t exec_type;
    uint8_t padding[7];
    uint64_t exec_id;
    uint64_t latency_ns;
};

static std::string get_exec_type_name(uint8_t type, uint64_t exec_id) {
    switch (type) {
        case 0: return "NEW";
        case 4: return "CANCEL";
        case 8: return "REJECT";
        case 5: {
            if (exec_id % 2 == 0) return "MODIFY_SHORT";
            else return "MODIFY_LONG";
        }
        default: return "UNKNOWN";
    }
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    auto *ev = static_cast<const latency_event *>(data);
    
    double ms = ev->latency_ns / 1000000.0;
    std::cout << get_exec_type_name(ev->exec_type, ev->exec_id) << ", " << ms << "\n" << std::flush;
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    struct total_lat_bpf *skel = total_lat_bpf__open();
    if (!skel) {
        std::cerr << "Failed to open skeleton\n";
        return 1;
    }

    if (total_lat_bpf__load(skel)) {
        std::cerr << "Failed to load skeleton\n";
        total_lat_bpf__destroy(skel);
        return 1;
    }

    if (total_lat_bpf__attach(skel)) {
        std::cerr << "Failed to attach skeleton kprobes\n";
        total_lat_bpf__destroy(skel);
        return 1;
    }

    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) {
        std::cerr << "Failed to create ring buffer\n";
        total_lat_bpf__destroy(skel);
        return 1;
    }

    while (keep_running) {
        ring_buffer__poll(rb, 100);
    }

    ring_buffer__free(rb);
    total_lat_bpf__destroy(skel);
    return 0;
}
