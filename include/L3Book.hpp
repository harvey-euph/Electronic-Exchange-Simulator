#pragma once
#include <map>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "fbs/order_generated.h"

namespace Exchange {

struct L3Order {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t qty;
};

struct L3Book {
    uint32_t symbol_id = 0;
    std::unordered_map<uint64_t, L3Order> orders;
    std::mutex mutex;

    void update(ExecType type, uint64_t order_id, Side side, int64_t price, uint64_t qty) {
        std::lock_guard<std::mutex> lock(mutex);
        
        switch (type) {
            case ExecType_New:
                orders[order_id] = {order_id, side, price, qty};
                break;
            case ExecType_Fill:
            case ExecType_Cancelled:
                orders.erase(order_id);
                break;
            case ExecType_PartialFill:
            case ExecType_Replaced:
                orders[order_id] = {order_id, side, price, qty};
                break;
            default:
                break;
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        orders.clear();
    }
};

} // namespace Exchange
