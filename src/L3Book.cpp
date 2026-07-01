#include "L3Book.hpp"
#include <fstream>

namespace Exchange {

std::vector<L2UpdateT> L3Book::update(ExecType type, uint64_t order_id, Side side, int64_t price, uint64_t qty) {
    std::vector<L2UpdateT> updates;
    std::lock_guard<std::mutex> lock(mutex);
    
    if (side == Side_None) {
        orders.clear();
        bids.clear();
        asks.clear();
        return updates;
    }
    
    switch (type) {
        case ExecType_New: {
            auto& level = (side == Side_Buy) ? bids[price] : asks[price];
            level.queue.push_back(order_id);
            level.total_qty += qty;
            orders[order_id] = {order_id, side, price, qty, qty, std::prev(level.queue.end())};
            
            updates.emplace_back(L2UpdateT{ .side = side, .p = price, .q = level.total_qty });
            break;
        }
        case ExecType_Fill:
        case ExecType_Cancelled: {
            remove_from_queues(order_id, updates);
            orders.erase(order_id);
            break;
        }
        case ExecType_PartialFill: {
            auto it = orders.find(order_id);
            if (it != orders.end()) {
                uint64_t new_total = 0;
                if (it->second.side == Side_Buy) {
                    auto level_it = bids.find(it->second.price);
                    if (level_it != bids.end()) {
                        level_it->second.total_qty -= qty;
                        new_total = level_it->second.total_qty;
                    }
                } else {
                    auto level_it = asks.find(it->second.price);
                    if (level_it != asks.end()) {
                        level_it->second.total_qty -= qty;
                        new_total = level_it->second.total_qty;
                    }
                }
                it->second.qty_rem -= qty;
                updates.emplace_back(L2UpdateT{ .side = it->second.side, .p = it->second.price, .q = new_total });
            }
            break;
        }
        case ExecType_Replaced: {
            auto it = orders.find(order_id);
            if (it != orders.end()) {
                int64_t qty_diff = static_cast<int64_t>(qty) - static_cast<int64_t>(it->second.qty_req);
                uint64_t new_rem = it->second.qty_rem + qty_diff;

                if (it->second.price != price || it->second.side != side || qty > it->second.qty_req) {
                    remove_from_queues(order_id, updates);
                    if (new_rem > 0) {
                        auto& level = (side == Side_Buy) ? bids[price] : asks[price];
                        level.queue.push_back(order_id);
                        level.total_qty += new_rem;
                        it->second = {order_id, side, price, qty, new_rem, std::prev(level.queue.end())};
                        
                        updates.emplace_back(L2UpdateT{ .side = side, .p = price, .q = level.total_qty });
                    } else {
                        orders.erase(it);
                    }
                } else {
                    auto& level = (side == Side_Buy) ? bids[price] : asks[price];
                    level.total_qty = level.total_qty + qty_diff;
                    it->second.qty_req = qty;
                    it->second.qty_rem = new_rem;
                    
                    updates.emplace_back(L2UpdateT{ .side = side, .p = price, .q = level.total_qty });
                    
                    if (new_rem == 0) {
                        remove_from_queues(order_id, updates);
                        orders.erase(it);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return updates;
}

void L3Book::remove_from_queues(uint64_t order_id, std::vector<L2UpdateT>& updates) {
    auto it = orders.find(order_id);
    if (it == orders.end()) return;

    uint64_t new_total = 0;
    int64_t old_price = it->second.price;
    Side old_side = it->second.side;

    if (old_side == Side_Buy) {
        auto level_it = bids.find(old_price);
        if (level_it != bids.end()) {
            if (level_it->second.total_qty >= it->second.qty_rem) {
                level_it->second.total_qty -= it->second.qty_rem;
            } else {
                level_it->second.total_qty = 0;
            }
            new_total = level_it->second.total_qty;
            level_it->second.queue.erase(it->second.queue_pos);
            if (level_it->second.queue.empty()) bids.erase(level_it);
        }
    } else if (old_side == Side_Sell) {
        auto level_it = asks.find(old_price);
        if (level_it != asks.end()) {
            if (level_it->second.total_qty >= it->second.qty_rem) {
                level_it->second.total_qty -= it->second.qty_rem;
            } else {
                level_it->second.total_qty = 0;
            }
            new_total = level_it->second.total_qty;
            level_it->second.queue.erase(it->second.queue_pos);
            if (level_it->second.queue.empty()) asks.erase(level_it);
        }
    }
    
    updates.emplace_back(L2UpdateT{ .side = old_side, .p = old_price, .q = new_total });
}

void L3Book::display(int depth_limit) {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "\033[2J\033[H"; // Clear screen and move to home
    
    int total_width = 80;
    int content_inner_width = total_width - 4;

    auto print_border = [&]() {
        std::cout << std::string(total_width, '*') << std::endl;
    };

    auto print_centered = [&](const std::string& text) {
        int padding = (content_inner_width - static_cast<int>(text.length())) / 2;
        if (padding < 0) padding = 0;
        std::cout << "* " << std::string(padding, ' ') << text 
                  << std::string(content_inner_width - text.length() - padding, ' ') << " *" << std::endl;
    };

    auto print_separator = [&]() {
        std::cout << "* " << std::string(content_inner_width, '-') << " *" << std::endl;
    };

    auto print_empty_row = [&](const std::string& msg) {
        int padding = (content_inner_width - static_cast<int>(msg.length())) / 2;
        std::cout << "* " << std::string(padding, ' ') << msg 
                  << std::string(content_inner_width - msg.length() - padding, ' ') << " *" << std::endl;
    };

    print_border();
    print_centered("L3 Order Book - Symbol: " + std::to_string(symbol_id));
    print_border();

    // Asks (Sorted Low to High, we print High to Low for top-down view)
    std::vector<int64_t> ask_prices;
    for (auto const& [price, _] : asks) {
        ask_prices.push_back(price);
        if (ask_prices.size() >= static_cast<size_t>(depth_limit)) break;
    }

    if (ask_prices.empty()) {
        print_empty_row("(No Asks)");
    } else {
        for (int i = static_cast<int>(ask_prices.size()) - 1; i >= 0; --i) {
            int64_t p = ask_prices[i];
            auto const& level = asks.at(p);
            uint64_t total_q = level.total_qty;
            std::stringstream ss;
            int o_count = 0;
            for (uint64_t oid : level.queue) {
                uint64_t oq = orders.at(oid).qty_rem;
                if (o_count < 5) {
                    if (o_count > 0) ss << " -> ";
                    ss << oq;
                }
                o_count++;
            }
            if (o_count > 5) ss << " -> ...";

            std::stringstream row;
            row << "A" << (i + 1) << " " << std::setw(10) << p << " " << std::setw(10) << total_q << " | " << ss.str();
            
            std::string row_str = row.str();
            if (row_str.length() > static_cast<size_t>(content_inner_width)) {
                row_str = row_str.substr(0, content_inner_width - 3) + "...";
            }
            std::cout << "* " << std::left << std::setw(content_inner_width) << row_str << " *" << std::endl;
        }
    }

    print_separator();

    // Bids (Sorted High to Low)
    if (bids.empty()) {
        print_empty_row("(No Bids)");
    } else {
        int i = 0;
        for (auto const& [p, level] : bids) {
            if (++i > depth_limit) break;
            uint64_t total_q = level.total_qty;
            std::stringstream ss;
            int o_count = 0;
            for (uint64_t oid : level.queue) {
                uint64_t oq = orders.at(oid).qty_rem;
                if (o_count < 5) {
                    if (o_count > 0) ss << " -> ";
                    ss << oq;
                }
                o_count++;
            }
            if (o_count > 5) ss << " -> ...";

            std::stringstream row;
            row << "B" << i << " " << std::setw(10) << p << " " << std::setw(10) << total_q << " | " << ss.str();
            
            std::string row_str = row.str();
            if (row_str.length() > static_cast<size_t>(content_inner_width)) {
                row_str = row_str.substr(0, content_inner_width - 3) + "...";
            }
            std::cout << "* " << std::left << std::setw(content_inner_width) << row_str << " *" << std::endl;
        }
    }

    print_border();
    std::cout << std::flush;
}

void L3Book::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    orders.clear();
    bids.clear();
    asks.clear();
}

void L3Book::dump_raw(const char* filepath) {
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream ofs(filepath);
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        ofs << "A," << it->first << "," << it->second.total_qty << "\n";
    }
    for (auto it = bids.begin(); it != bids.end(); ++it) {
        ofs << "B," << it->first << "," << it->second.total_qty << "\n";
    }
}

} // namespace Exchange
