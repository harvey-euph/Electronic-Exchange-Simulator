#pragma once

#include "client/L3Book.hpp"

namespace Exchange {

struct MDOrderBook : public L3Book {
    std::unordered_map<int64_t, uint64_t> pending_l2_bids;
    std::unordered_map<int64_t, uint64_t> pending_l2_asks;
    std::chrono::steady_clock::time_point last_l2_publish_time_;
    OrderResponseT pending_order;

    MDOrderBook() {
        pending_order.order_id = 0;
    }

    void on_level_updated(Side side, int64_t price, uint64_t total_qty) override {
        if (side == Side_Buy) pending_l2_bids[price] = total_qty;
        else if (side == Side_Sell) pending_l2_asks[price] = total_qty;
    }

    void clear() override {
        L3Book::clear();
        pending_l2_bids.clear();
        pending_l2_asks.clear();
    }

    std::vector<L2UpdateT> extract_pending_l2_updates(std::chrono::steady_clock::time_point now, int throttle_ms) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<L2UpdateT> updates;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_l2_publish_time_).count();
        if (elapsed < throttle_ms) return updates;

        updates.reserve(pending_l2_bids.size() + pending_l2_asks.size());
        for (auto const& [p, q] : pending_l2_bids) updates.push_back(L2UpdateT{.side = Side_Buy, .p = p, .q = q});
        for (auto const& [p, q] : pending_l2_asks) updates.push_back(L2UpdateT{.side = Side_Sell, .p = p, .q = q});
        
        if (!updates.empty()) {
            pending_l2_bids.clear();
            pending_l2_asks.clear();
            last_l2_publish_time_ = now;
        }
        return updates;
    }
};

} // namespace Exchange
