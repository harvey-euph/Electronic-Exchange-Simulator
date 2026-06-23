#pragma once
#include <map>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <list>
#include <iostream>
#include <iomanip>
#include <vector>
#include <sstream>
#include <string>
#include "fbs/exchange_generated.h"

namespace Exchange {

struct L3Order {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t qty_req;
    uint64_t qty_rem;
    std::list<uint64_t>::iterator queue_pos;
};

struct L3PriceLevel {
    uint64_t total_qty = 0;
    std::list<uint64_t> queue; // order IDs
};

struct L3Book {
    uint32_t symbol_id = 0;
    std::unordered_map<uint64_t, L3Order> orders;

    // Price levels with queue of order IDs and total quantity
    std::map<int64_t, L3PriceLevel, std::greater<int64_t>> bids;
    std::map<int64_t, L3PriceLevel> asks;
    
    std::mutex mutex;

    std::vector<L2UpdateT> update(ExecType type, uint64_t order_id, Side side, int64_t price, uint64_t qty);
    void remove_from_queues(uint64_t order_id, std::vector<L2UpdateT>& updates);
    void display(int depth_limit = 10);
    void clear();
};

} // namespace Exchange
