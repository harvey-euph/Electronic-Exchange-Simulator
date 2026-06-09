#include <iostream>
#include <vector>
#include <iomanip>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <string>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <bpf/libbpf.h>
#include "ws_monitor.skel.h"
#include "fbs/order_generated.h"
#include "define.hpp"

struct event_data {
    uint64_t sock_ptr;
    uint64_t timestamp_ns;
    uint32_t len;
    uint8_t event_type; // 0: RX (recv), 1: TX (send)
    uint8_t padding[3];  // Explicit padding to align to 8 bytes boundary
    uint8_t payload[512];
};

static volatile bool keep_running = true;

static void sig_handler(int signo) {
    keep_running = false;
}

struct PairHash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

static std::unordered_map<std::pair<uint32_t, uint64_t>, uint64_t, PairHash> pending_requests;

struct LatencyStats {
    std::vector<uint64_t> samples;
    uint64_t max_val = 0;
    uint64_t total_count = 0;
    uint64_t historical_max = 0;
    
    void add(uint64_t latency_ns) {
        samples.push_back(latency_ns);
        total_count++;
        if (latency_ns > max_val) {
            max_val = latency_ns;
        }
        if (latency_ns > historical_max) {
            historical_max = latency_ns;
        }
    }
    
    void clear() {
        samples.clear();
        max_val = 0;
    }
};

static std::unordered_map<Exchange::ExecType, LatencyStats> stats_by_type;
static LatencyStats global_stats;

static int last_printed_lines = 0;

static void print_stats_table() {
    bool has_any = false;
    for (const auto& pair : stats_by_type) {
        if (pair.second.total_count > 0) {
            has_any = true;
            break;
        }
    }
    if (!has_any) return;

    // Move cursor up to overwrite previous table if it was printed before
    if (last_printed_lines > 0) {
        std::cout << "\033[" << last_printed_lines << "A\033[J";
    }

    int printed_lines = 0;

    std::cout << "=================================== Latency Statistics (us) ===================================\n";
    printed_lines++;
    std::cout << std::left << std::setw(15) << "Exec Type"
              << std::right << std::setw(12) << "Int Count"
              << std::right << std::setw(12) << "Total Count"
              << std::right << std::setw(10) << "P50"
              << std::right << std::setw(10) << "P90"
              << std::right << std::setw(10) << "P99"
              << std::right << std::setw(10) << "Max"
              << std::right << std::setw(12) << "Hist Max" << "\n";
    printed_lines++;
    std::cout << "-----------------------------------------------------------------------------------------------\n";
    printed_lines++;

    auto print_row = [&](const std::string& label, LatencyStats& s) {
        if (s.total_count == 0) return;
        
        double p50 = 0.0, p90 = 0.0, p99 = 0.0, max_val = 0.0;
        if (!s.samples.empty()) {
            std::sort(s.samples.begin(), s.samples.end());
            size_t n = s.samples.size();
            p50 = s.samples[n * 0.50] / 1000.0;
            p90 = s.samples[n * 0.90] / 1000.0;
            p99 = s.samples[n * 0.99] / 1000.0;
            max_val = s.max_val / 1000.0;
        }
        double hist_max = s.historical_max / 1000.0;
        
        std::cout << std::left << std::setw(15) << label
                  << std::right << std::setw(12) << s.samples.size()
                  << std::right << std::setw(12) << s.total_count
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(10) << p50
                  << std::setw(10) << p90
                  << std::setw(10) << p99
                  << std::setw(10) << max_val
                  << std::right << std::setw(12) << hist_max << "\n";
        printed_lines++;
    };

    for (auto& pair : stats_by_type) {
        std::string label = Exchange::EnumNameExecType(pair.first);
        print_row(label, pair.second);
    }
    
    std::cout << "-----------------------------------------------------------------------------------------------\n";
    printed_lines++;
    print_row("ALL", global_stats);
    std::cout << "===============================================================================================\n" << std::flush;
    printed_lines++;

    last_printed_lines = printed_lines;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -p <port>          Specify TCP port to monitor directly (default: 9001)\n"
              << "  -h, --help         Show this help message\n";
}

static void handle_rx_event(event_data *ev, size_t safe_len) {
    const uint8_t* ptr = ev->payload;
    size_t remaining = safe_len;
    
    while (remaining > 2) {
        uint8_t opcode = ptr[0] & 0x0F;
        uint8_t mask = (ptr[1] & 0x80) >> 7;
        uint8_t payload_len_field = ptr[1] & 0x7F;
        
        if (opcode != 0x2) {
            break;
        }
        
        size_t header_len = 2;
        uint64_t actual_payload_len = payload_len_field;
        
        if (payload_len_field == 126) {
            if (remaining < 4) break;
            actual_payload_len = (ptr[2] << 8) | ptr[3];
            header_len = 4;
        } else if (payload_len_field == 127) {
            if (remaining < 10) break;
            actual_payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                actual_payload_len = (actual_payload_len << 8) | ptr[2 + i];
            }
            header_len = 10;
        }
        
        size_t frame_total_len = header_len + (mask ? 4 : 0) + actual_payload_len;
        size_t payload_offset = header_len + (mask ? 4 : 0);
        
        if (remaining <= payload_offset) {
            if (frame_total_len == 0 || frame_total_len >= remaining) break;
            ptr += frame_total_len;
            remaining -= frame_total_len;
            continue;
        }
        
        size_t avail_payload_len = std::min(remaining - payload_offset, (size_t)actual_payload_len);
        std::vector<uint8_t> decoded(avail_payload_len);
        if (mask == 1) {
            uint8_t masking_key[4];
            for (int i = 0; i < 4; ++i) {
                masking_key[i] = ptr[header_len + i];
            }
            for (size_t i = 0; i < avail_payload_len; ++i) {
                decoded[i] = ptr[payload_offset + i] ^ masking_key[i % 4];
            }
        } else {
            for (size_t i = 0; i < avail_payload_len; ++i) {
                decoded[i] = ptr[payload_offset + i];
            }
        }
        
        [&]() {
            if (decoded.size() <= sizeof(flatbuffers::uoffset_t)) return;
            
            flatbuffers::Verifier verifier(decoded.data(), decoded.size());
            if (!verifier.VerifyBuffer<Exchange::ClientRequest>(nullptr)) return;
            
            auto req = flatbuffers::GetRoot<Exchange::ClientRequest>(decoded.data());
            if (!req || req->data_type() != Exchange::ClientRequestData_OrderRequest) return;
            
            auto order_req = req->data_as_OrderRequest();
            if (!order_req) return;
            
            uint32_t client_id = order_req->client_id();
            uint64_t order_id = order_req->order_id();
            
            pending_requests[{client_id, order_id}] = ev->timestamp_ns;
        }();
        
        if (frame_total_len == 0 || frame_total_len >= remaining) {
            break;
        }
        ptr += frame_total_len;
        remaining -= frame_total_len;
    }
}

static void handle_tx_event(event_data *ev, size_t safe_len) {
    const uint8_t* ptr = ev->payload;
    size_t remaining = safe_len;
    
    while (remaining > 2) {
        uint8_t opcode = ptr[0] & 0x0F;
        uint8_t mask = (ptr[1] & 0x80) >> 7;
        uint8_t payload_len_field = ptr[1] & 0x7F;
        
        if (opcode != 0x2) {
            break;
        }
        
        size_t header_len = 2;
        uint64_t actual_payload_len = payload_len_field;
        
        if (payload_len_field == 126) {
            if (remaining < 4) break;
            actual_payload_len = (ptr[2] << 8) | ptr[3];
            header_len = 4;
        } else if (payload_len_field == 127) {
            if (remaining < 10) break;
            actual_payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                actual_payload_len = (actual_payload_len << 8) | ptr[2 + i];
            }
            header_len = 10;
        }
        
        size_t frame_total_len = header_len + (mask ? 4 : 0) + actual_payload_len;
        size_t payload_offset = header_len + (mask ? 4 : 0);
        
        if (remaining <= payload_offset) {
            if (frame_total_len == 0 || frame_total_len >= remaining) break;
            ptr += frame_total_len;
            remaining -= frame_total_len;
            continue;
        }
        
        size_t avail_payload_len = std::min(remaining - payload_offset, (size_t)actual_payload_len);
        std::vector<uint8_t> decoded(avail_payload_len);
        if (mask == 1) {
            uint8_t masking_key[4];
            for (int i = 0; i < 4; ++i) {
                masking_key[i] = ptr[header_len + i];
            }
            for (size_t i = 0; i < avail_payload_len; ++i) {
                decoded[i] = ptr[payload_offset + i] ^ masking_key[i % 4];
            }
        } else {
            for (size_t i = 0; i < avail_payload_len; ++i) {
                decoded[i] = ptr[payload_offset + i];
            }
        }
        
        [&]() {
            if (decoded.size() <= sizeof(flatbuffers::uoffset_t)) return;
            
            flatbuffers::Verifier verifier(decoded.data(), decoded.size());
            if (!verifier.VerifyBuffer<Exchange::ClientResponse>(nullptr)) return;
            
            auto resp = flatbuffers::GetRoot<Exchange::ClientResponse>(decoded.data());
            if (!resp || resp->data_type() != Exchange::ClientResponseData_OrderResponse) return;
            
            auto order_resp = resp->data_as_OrderResponse();
            if (!order_resp) return;
            
            Exchange::ExecType exec_type = order_resp->exec_type();
            if ((~Exchange::EXEC_MASK_LATENCY_TRACK >> exec_type) & 1) return;

            uint32_t client_id = order_resp->client_id();
            uint64_t order_id = order_resp->order_id();
            
            auto it = pending_requests.find({client_id, order_id});
            if (it == pending_requests.end()) return;
            
            uint64_t latency_ns = ev->timestamp_ns - it->second;
            
            pending_requests.erase(it);
            
            stats_by_type[exec_type].add(latency_ns);
            global_stats.add(latency_ns);
        }();
        
        if (frame_total_len == 0 || frame_total_len >= remaining) {
            break;
        }
        ptr += frame_total_len;
        remaining -= frame_total_len;
    }
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    if (data_sz < sizeof(event_data)) {
        std::cout << "[eBPF Monitor] ERROR: Received event with size " << data_sz 
                  << " which is smaller than expected " << sizeof(event_data) << std::endl;
        return 0;
    }
    auto *ev = static_cast<event_data*>(data);

    if (ev->event_type != 0 && ev->event_type != 1) {
        return 0;
    }

    size_t safe_len = std::min((size_t)ev->len, sizeof(ev->payload));

    if (ev->event_type == 0) {
        handle_rx_event(ev, safe_len);
    } else {
        handle_tx_event(ev, safe_len);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    uint16_t selected_port = 9001;

    int opt;
    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        switch (opt) {
            case 'p':
                try {
                    selected_port = static_cast<uint16_t>(std::stoul(optarg));
                } catch (...) {
                    std::cerr << "Invalid port number: " << optarg << "\n";
                    return 1;
                }
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    // libbpf_set_print(nullptr);

    struct ws_monitor_bpf *skel = ws_monitor_bpf__open();
    if (!skel) {
        std::cerr << "Failed to open BPF skeleton\n";
        return 1;
    }

    skel->rodata->target_port = selected_port;

    int err = ws_monitor_bpf__load(skel);
    if (err) {
        std::cerr << "Failed to load BPF skeleton\n";
        ws_monitor_bpf__destroy(skel);
        return 1;
    }

    err = ws_monitor_bpf__attach(skel);
    if (err) {
        std::cerr << "Failed to attach BPF skeleton\n";
        ws_monitor_bpf__destroy(skel);
        return 1;
    }

    struct ring_buffer *ring_buf = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, nullptr, nullptr);
    if (!ring_buf) {
        std::cerr << "Failed to create ring buffer manager\n";
        ws_monitor_bpf__destroy(skel);
        return 1;
    }

    if (selected_port == 0) {
        std::cout << "[eBPF Monitor] Started. Monitoring ALL ports for WebSocket RX->TX latency...\n";
    } else {
        std::cout << "[eBPF Monitor] Started. Monitoring TCP port " << selected_port << " for WebSocket RX->TX latency...\n";
    }
    std::cout << "Press Ctrl+C to exit.\n\n";

    auto last_print = std::chrono::steady_clock::now();
    while (keep_running) {
        err = ring_buffer__poll(ring_buf, 100);
        if (err < 0 && err != -EINTR) {
            std::cerr << "Error polling ring buffer\n";
            break;
        }
        
        auto now = std::chrono::steady_clock::now();
        if (now - last_print >= std::chrono::seconds(1)) {
            print_stats_table();
            // Clear interval samples
            for (auto& pair : stats_by_type) {
                pair.second.clear();
            }
            global_stats.clear();
            last_print = now;
        }
    }

    ring_buffer__free(ring_buf);
    ws_monitor_bpf__destroy(skel);
    
    std::cout << "\n[eBPF Monitor] Final Latency Summary:\n";
    last_printed_lines = 0;
    print_stats_table();
    
    std::cout << "[eBPF Monitor] Stopped.\n";
    return 0;
}
