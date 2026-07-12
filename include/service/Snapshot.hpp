#pragma once
#include <cstdint>
#include "fbs/exchange_generated.h"

namespace Exchange {

struct OrderSnapshot {
    uint64_t combined_order_id;
    uint64_t qty_original;
    uint64_t qty_remaining;
    OrderType type;
    Side side;
    uint64_t timestamp;
    uint32_t symbol_id;
    int64_t p;
};

} // namespace Exchange
