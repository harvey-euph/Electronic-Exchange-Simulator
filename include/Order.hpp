#pragma once
#include <cstdint>
#include "fbs/exchange_generated.h"

namespace Exchange {

struct PriceLevel; // Forward declaration

struct Order
{
    uint64_t exec_id;
    uint64_t order_id;
    uint32_t client_id;
    uint64_t qty_original;
    uint64_t qty_remaining;
    // uint64_t qty_visible;  // For Iceberg

    OrderType type; // Only consider limit and market order for now

    Order* prev = nullptr;
    Order* next = nullptr;

    PriceLevel* price_level = nullptr;

    uint64_t timestamp;
    uint32_t symbol_id = 0;
    Order(uint64_t exec_id_ = 0, uint64_t order_id_ = 0, uint32_t client_id_ = 0, 
          uint64_t qty_original_ = 0, uint64_t qty_remaining_ = 0, OrderType type_ = OrderType_Limit,
          Order* prev_ = nullptr, Order* next_ = nullptr, PriceLevel* price_level_ = nullptr, 
          uint64_t timestamp_ = 0, uint32_t symbol_id_ = 0)
        : exec_id(exec_id_), order_id(order_id_), client_id(client_id_),
          qty_original(qty_original_), qty_remaining(qty_remaining_), type(type_),
          prev(prev_), next(next_), price_level(price_level_), 
          timestamp(timestamp_), symbol_id(symbol_id_) {}
};

} // namespace Exchange
