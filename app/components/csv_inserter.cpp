#include "ring/SHMRingBuffer.hpp"
#include "mmap_log.h"
#include "define.hpp"
#include "fbs/exchange_generated.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace Exchange;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file>\n";
        return 1;
    }

    std::string filename = argv[1];
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << "\n";
        return 1;
    }

    SHMRingBuffer request_ring(ORDER_REQUEST, ORDER_REQUEST_SIZE);

    // wait for matching engine to init SHM
    while (request_ring.get_capacity() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string line;
    // Read header
    if (!std::getline(file, line)) {
        std::cerr << "Empty file\n";
        return 1;
    }

    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> cols;
        while (std::getline(ss, token, ',')) {
            cols.push_back(token);
        }

        if (cols.size() < 11) {
            std::cerr << "Warning: skipping invalid line: " << line << "\n";
            continue;
        }

        try {
            uint64_t req_id = std::stoull(cols[0]);
            uint64_t order_id = std::stoull(cols[1]);
            uint32_t client_id = std::stoul(cols[2]);
            std::string action_str = cols[3];
            std::string side_str = cols[4];
            std::string type_str = cols[5];
            int64_t price = std::stoll(cols[6]);
            uint64_t qty = std::stoull(cols[7]);
            // uint64_t visible_qty = std::stoull(cols[8]);
            uint64_t timestamp = std::stoull(cols[9]);
            uint32_t symbol_id = std::stoul(cols[10]);

            auto token_slot = request_ring.reserve(sizeof(OrderRequestT));
            while (!token_slot) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                token_slot = request_ring.reserve(sizeof(OrderRequestT));
            }

            OrderRequestT* req = new (token_slot->payload) OrderRequestT;
            req->client_id = client_id;
            req->symbol_id = symbol_id;
            req->timestamp = timestamp;
            req->msg_seq_num = req_id;
            req->exec_id = req_id;
            req->order_id = order_id;
            req->p = price;
            req->q = qty;

            if (action_str == "New") req->action = OrderAction_New;
            else if (action_str == "Modify") req->action = OrderAction_Modify;
            else if (action_str == "Cancel") req->action = OrderAction_Cancel;

            if (side_str == "Buy") req->side = Side_Buy;
            else if (side_str == "Sell") req->side = Side_Sell;

            if (type_str == "Limit") req->type = OrderType_Limit;
            else if (type_str == "Market") req->type = OrderType_Market;

            request_ring.commit(*token_slot);
            count++;
        } catch (const std::exception& e) {
            std::cerr << "Warning: error parsing line: " << line << " (" << e.what() << ")\n";
        }
    }

    std::cout << "Inserted " << count << " rows from " << filename << " into request ring.\n";
    return 0;
}
