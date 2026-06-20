#include "AlgoTradingClient.hpp"
#include "HttpUtil.hpp"
#include "JsonUtil.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>
#include <atomic>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <openssl/ssl.h>

namespace Exchange {

class MarketMakerNative : public AlgoTradingClient {
public:
    MarketMakerNative(const Config& config) : AlgoTradingClient(config) {
        std::cout << "[MM-Native] Started. ClientID=" << config_.client_id << std::endl;
        
        price_thread_ = std::thread(&MarketMakerNative::fetch_binance_price_loop, this);
    }

    ~MarketMakerNative() {
        thread_running_ = false;
        if (price_thread_.joinable()) {
            price_thread_.join();
        }
    }

    void on_l2_update(const L2Update*) override {}
    void on_l3_update(const L3Update*) override {}

    void on_timer() override {
        double current_mid = 0.0;
        {
            std::lock_guard<std::mutex> lock(mid_price_mtx_);
            current_mid = mm_mid_price_;
        }

        if (current_mid <= 0.0) {
            return; // Wait until we fetch the first price
        }

        auto it = symbols_info_.find(1);
        [[maybe_unused]] int64_t step = (it != symbols_info_.end()) ? it->second->price_min_step : 1;

        tick_count_++;

        double last_price = current_mid;
        
        // 2. Market Simulation Logic
        // For a stable market maker, estimation is exactly the Binance price
        double estimation = last_price;
        if (it != symbols_info_.end()) {
            const auto& info = it->second;
            estimation = std::max(static_cast<double>(info->price_min), std::min(static_cast<double>(info->price_max), estimation));
        }

        // 3. Dynamic Order Management
        manage_orders(estimation);

        static int count = 0;
        if (++count % 10 == 0) {
            std::cout << "[MM-Native] Binance Mid: " << std::fixed << std::setprecision(2) << current_mid 
                      << " | Est: " << estimation 
                      << " | Spread Mult: " << spread_multiplier_
                      << " | Active Orders: " << account_.get_open_orders().size() << std::endl;
        }
    }

private:

    void manage_orders(double estimation) {
        auto open_orders = account_.get_open_orders();
        
        auto it = symbols_info_.find(1);
        int64_t step = (it != symbols_info_.end()) ? it->second->price_min_step : 1;

        // Initialize last_estimation_ if first tick
        if (last_estimation_ == 0.0) {
            last_estimation_ = estimation;
        }

        // Calculate price movement in steps
        double price_change = std::abs(estimation - last_estimation_);
        double price_change_in_steps = price_change / step;

        // If price change is significant, widen the spread multiplier
        if (price_change_in_steps > 1.0) {
            spread_multiplier_ += price_change_in_steps * 0.4;
        }

        // Clamp spread_multiplier_
        spread_multiplier_ = std::min(max_spread_multiplier_, std::max(1.0, spread_multiplier_));

        // Decay the multiplier back to 1.0
        spread_multiplier_ = 1.0 + (spread_multiplier_ - 1.0) * decay_rate_;

        // Update last_estimation_
        last_estimation_ = estimation;

        int bids = 0, asks = 0;

        // 200ms fluctuation boundary
        bool is_fluct_tick = (tick_count_ % 2 == 0);

        // 1. Handle existing orders: Retreat or pull closer (Never call cancel_order!)
        for (const auto& o : open_orders) {
            double p = static_cast<double>(o.p);

            if (o.side == Side_Buy) {
                bids++;
                // Check if the order is out of the reasonable window:
                // Too close to estimation (swept/cross risk) or too far away from estimation
                if (p > estimation - 1.0 * step * spread_multiplier_ || p < estimation - 25.0 * step * spread_multiplier_) {
                    double new_p = estimation - base_spread_ * step * spread_multiplier_ - std::abs(dist_noise_(gen_)) * step;
                    int64_t rounded_new_p = std::round(new_p / step) * step;
                    if (it != symbols_info_.end()) {
                        rounded_new_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_new_p));
                    }
                    // Avoid crossing spread (must be strictly less than estimation)
                    if (rounded_new_p >= estimation) {
                        rounded_new_p = std::round((estimation - 1.0 * step) / step) * step;
                    }
                    uint64_t new_q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
                    replace_order(o.order_id, rounded_new_p, new_q, o.symbol_id, o.side);
                } 
                // If the order is in the reasonable window, and this is fluctuation tick, slightly jitter it
                else if (is_fluct_tick && std::uniform_real_distribution<>(0.0, 1.0)(gen_) < 0.70) {
                    // Small price jitter: +/- 1 or 2 steps
                    int price_jitter = std::uniform_int_distribution<int>(-2, 2)(gen_);
                    double jittered_p = p + price_jitter * step;
                    int64_t rounded_new_p = std::round(jittered_p / step) * step;
                    
                    // Clamp to symbol price limits
                    if (it != symbols_info_.end()) {
                        rounded_new_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_new_p));
                    }
                    
                    // Ensure buy price remains strictly below estimation to prevent crossing
                    if (rounded_new_p >= estimation) {
                        rounded_new_p = std::round((estimation - 1.0 * step) / step) * step;
                    }

                    // Small qty jitter: +/- 10% to 20% or flat random
                    int64_t qty_change = std::uniform_int_distribution<int>(-5, 5)(gen_);
                    int64_t new_q = static_cast<int64_t>(o.q) + qty_change;
                    if (new_q < 5) new_q = 5;
                    if (new_q > 50) new_q = 50;

                    // Only replace if something changed
                    if (rounded_new_p != o.p || new_q != static_cast<int64_t>(o.q)) {
                        replace_order(o.order_id, rounded_new_p, new_q, o.symbol_id, o.side);
                    }
                }
            } else if (o.side == Side_Sell) {
                asks++;
                // Check if the order is out of the reasonable window:
                // Too close to estimation (swept/cross risk) or too far away from estimation
                if (p < estimation + 1.0 * step * spread_multiplier_ || p > estimation + 25.0 * step * spread_multiplier_) {
                    double new_p = estimation + base_spread_ * step * spread_multiplier_ + std::abs(dist_noise_(gen_)) * step;
                    int64_t rounded_new_p = std::round(new_p / step) * step;
                    if (it != symbols_info_.end()) {
                        rounded_new_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_new_p));
                    }
                    // Avoid crossing spread (must be strictly greater than estimation)
                    if (rounded_new_p <= estimation) {
                        rounded_new_p = std::round((estimation + 1.0 * step) / step) * step;
                    }
                    uint64_t new_q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
                    replace_order(o.order_id, rounded_new_p, new_q, o.symbol_id, o.side);
                }
                // If the order is in the reasonable window, and this is fluctuation tick, slightly jitter it
                else if (is_fluct_tick && std::uniform_real_distribution<>(0.0, 1.0)(gen_) < 0.70) {
                    // Small price jitter: +/- 1 or 2 steps
                    int price_jitter = std::uniform_int_distribution<int>(-2, 2)(gen_);
                    double jittered_p = p + price_jitter * step;
                    int64_t rounded_new_p = std::round(jittered_p / step) * step;
                    
                    // Clamp to symbol price limits
                    if (it != symbols_info_.end()) {
                        rounded_new_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_new_p));
                    }
                    
                    // Ensure sell price remains strictly above estimation to prevent crossing
                    if (rounded_new_p <= estimation) {
                        rounded_new_p = std::round((estimation + 1.0 * step) / step) * step;
                    }

                    // Small qty jitter: +/- 10% to 20% or flat random
                    int64_t qty_change = std::uniform_int_distribution<int>(-5, 5)(gen_);
                    int64_t new_q = static_cast<int64_t>(o.q) + qty_change;
                    if (new_q < 5) new_q = 5;
                    if (new_q > 50) new_q = 50;

                    // Only replace if something changed
                    if (rounded_new_p != o.p || new_q != static_cast<int64_t>(o.q)) {
                        replace_order(o.order_id, rounded_new_p, new_q, o.symbol_id, o.side);
                    }
                }
            }
        }

        // 2. Replenish Liquidity: Maintain at least 12 orders on each side
        int target_per_side = 12;
        for (int i = bids; i < target_per_side; ++i) {
            double p = estimation - base_spread_ * step * spread_multiplier_ - std::abs(dist_noise_(gen_)) * step;
            int64_t rounded_p = std::round(p / step) * step;
            if (it != symbols_info_.end()) {
                rounded_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_p));
            }
            if (rounded_p >= estimation) {
                rounded_p = std::round((estimation - 1.0 * step) / step) * step;
            }
            uint64_t q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
            new_limit_order(1, Side_Buy, rounded_p, q);
        }
        for (int i = asks; i < target_per_side; ++i) {
            double p = estimation + base_spread_ * step * spread_multiplier_ + std::abs(dist_noise_(gen_)) * step;
            int64_t rounded_p = std::round(p / step) * step;
            if (it != symbols_info_.end()) {
                rounded_p = std::max(it->second->price_min, std::min(it->second->price_max, rounded_p));
            }
            if (rounded_p <= estimation) {
                rounded_p = std::round((estimation + 1.0 * step) / step) * step;
            }
            uint64_t q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
            new_limit_order(1, Side_Sell, rounded_p, q);
        }
    }

    std::thread price_thread_;
    std::atomic<bool> thread_running_{true};

    void fetch_binance_price_loop() {
        namespace beast = boost::beast;
        namespace http = beast::http;

        while (thread_running_) {
            try {
                std::string body = perform_https_request(
                    "api.binance.com", "443",
                    http::verb::get,
                    "/api/v3/ticker/price?symbol=BTCUSDT"
                );

                std::string price_str = get_json_string(body, "price");
                if (!price_str.empty()) {
                    double price = std::stod(price_str);
                    
                    double scale = 100.0;
                    auto it = symbols_info_.find(1);
                    if (it != symbols_info_.end()) {
                        scale = std::pow(10, -it->second->price_exp);
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(mid_price_mtx_);
                        mm_mid_price_ = price * scale;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[MM-Native] Error fetching Binance price: " << e.what() << std::endl;
            }

            // Fetch every 1 second
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::mt19937 gen_{std::random_device{}()};
    std::normal_distribution<> dist_price_walk_{0, 1.5};
    std::normal_distribution<> dist_estimation_{0, 3.0};
    std::normal_distribution<> dist_noise_{0, 5.0};

    // Stable market maker states
    double last_estimation_ = 0.0;
    double spread_multiplier_ = 1.0;
    const double base_spread_ = 1.5;
    const double max_spread_multiplier_ = 8.0;
    const double decay_rate_ = 0.95; // decay 5% each tick (100ms)

    int tick_count_ = 0;
    double mm_mid_price_ = 0.0;
    std::mutex mid_price_mtx_;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 100; // Native Market Maker
    config.symbol_ids = {1};
    config.timer_interval_ms = 100; // Faster tick (100ms)

    Exchange::MarketMakerNative mm(config);
    return mm.run();
}
