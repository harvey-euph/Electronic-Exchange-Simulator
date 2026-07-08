#include "ring/SHMRingBuffer.hpp"
#include "mmap_log.h"
#include "define.hpp"
#include "fbs/exchange_generated.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <vector>
#include <unordered_set>

using namespace Exchange;

enum Phase { INSERTING, TARGET, CLEANING };

int main(int argc, char* argv[]) {
    int N = 10000;
    if (argc > 1) {
        N = std::stoi(argv[1]);
    }

    SHMRingBuffer request_ring(ORDER_REQUEST "_1", ORDER_REQUEST_SIZE);
    mmaplog::MmapReader response_ring(EXECUTION_JOURNAL_DIR);
    
    // wait for matching engine to init SHM
    while (request_ring.get_capacity() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::mt19937 gen(1337);
    std::uniform_real_distribution<> dist(0.0, 1.0);
    std::uniform_int_distribution<int> bool_dist(0, 1);
    std::uniform_int_distribution<int> dist_price(-500, 500); // spread around 5000
    
    std::vector<uint64_t> open_orders;
    uint64_t next_order_id = 1;
    uint64_t next_exec_id = 1;

    const uint64_t TEST_MSG_SEQ_BASE = 1000000000ULL;

    auto run_phase = [&](Phase phase, int target_orders, const std::string& phase_name) {
        int orders_sent = 0;
        std::unordered_set<uint64_t> pending_exec_ids;
        
        auto t_start = std::chrono::steady_clock::now();
        bool first_sent = false;

        std::cout << "Starting " << phase_name << " phase (" << target_orders << " orders)..." << std::endl;

        while (orders_sent < target_orders || !pending_exec_ids.empty()) {
            // Send orders
            if (orders_sent < target_orders) {
                if (request_ring.get_occupancy_ratio() > 0.9) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                } else {
                    auto token = request_ring.reserve(sizeof(OrderRequestT));
                    if (token) {
                        OrderRequestT* req = new (token->payload) OrderRequestT;
                        req->client_id = 1;
                        req->symbol_id = 1;
                        req->timestamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        req->exec_id = TEST_MSG_SEQ_BASE + next_exec_id;
                        next_exec_id++;
                        
                        if (phase == INSERTING) {
                            req->action = OrderAction_New;
                            req->order_id = next_order_id++;
                            req->side = bool_dist(gen) ? Side_Buy : Side_Sell;
                            req->type = OrderType_Limit;
                            req->p = 5000 + dist_price(gen);
                            req->q = 10;
                            open_orders.push_back(req->order_id);
                        } else if (phase == CLEANING) {
                            req->action = OrderAction_Cancel;
                            req->order_id = open_orders.back();
                            open_orders.pop_back();
                        } else {
                            double roll = dist(gen);
                            if (open_orders.empty() || roll < 0.30) {
                                // 30% New
                                req->action = OrderAction_New;
                                req->order_id = next_order_id++;
                                req->side = bool_dist(gen) ? Side_Buy : Side_Sell;
                                req->type = OrderType_Limit;
                                req->p = 5000 + dist_price(gen);
                                req->q = 10;
                                open_orders.push_back(req->order_id);
                            } else if (roll < 0.70) {
                                // 40% Modify
                                req->action = OrderAction_Modify;
                                req->order_id = open_orders[gen() % open_orders.size()];
                                req->side = Side_Buy; 
                                req->type = OrderType_Limit;
                                req->p = 5000 + dist_price(gen);
                                req->q = 5;
                            } else {
                                // 30% Cancel
                                req->action = OrderAction_Cancel;
                                size_t idx = gen() % open_orders.size();
                                req->order_id = open_orders[idx];
                                open_orders[idx] = open_orders.back();
                                open_orders.pop_back();
                            }
                        }
                        
                        pending_exec_ids.insert(req->exec_id);
                        
                        request_ring.commit(*token);
                        orders_sent++;
                        if (!first_sent) {
                            t_start = std::chrono::steady_clock::now();
                            first_sent = true;
                        }
                    }
                }
            }
            
            // Receive responses
            const void* data = nullptr;
            uint32_t len = 0;
            if (response_ring.read_next(data, len)) {
                if (len == sizeof(OrderResponseT)) {
                    const OrderResponseT* resp = static_cast<const OrderResponseT*>(data);
                    auto exec = resp->exec_type;
                    if (exec == ExecType_New || exec == ExecType_Replaced || exec == ExecType_Cancelled || exec == ExecType_Rejected) {
                        pending_exec_ids.erase(resp->exec_id);
                    }
                }
            }
        }
        
        auto t_end = std::chrono::steady_clock::now();
        double elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
        double elapsed_s = elapsed_us / 1000000.0;
        
        double tps = target_orders / elapsed_s;
        double mtps = tps / 1000000.0;
        std::cout << "========================================" << std::endl;
        std::cout << "Benchmark Results (" << phase_name << " Phase)" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total Orders : " << target_orders << std::endl;
        std::cout << "Time spent   : " << elapsed_s << " s" << std::endl;
        std::cout << "Throughput   : " << mtps << " million TPS" << std::endl;
        std::cout << "========================================" << std::endl;
    };

    int PREP_ORDERS = N * 0.2;
    int TARGET_ORDERS = N;

    run_phase(INSERTING, PREP_ORDERS, "Inserting");
    run_phase(TARGET, TARGET_ORDERS, "Target");
    run_phase(CLEANING, open_orders.size(), "Cleaning");

    return 0;
}
