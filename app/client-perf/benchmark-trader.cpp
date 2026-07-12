#include "PerfTraderBase.hpp"
#include <iostream>
#include <csignal>

namespace Exchange {

volatile sig_atomic_t g_stop = 0;

class BenchmarkTrader : public PerfTraderBase {
public:
    BenchmarkTrader(const Config& config, uint64_t target_requests, bool silent = false) 
        : PerfTraderBase(config, silent), target_requests_(target_requests) 
    {
        benchmark_thread_ = std::thread(&BenchmarkTrader::benchmark_loop, this);
    }

    ~BenchmarkTrader() override {
        running_ = false;
        if (benchmark_thread_.joinable()) {
            benchmark_thread_.join();
        }
    }

    void on_l2_update(uint32_t symbol_id, const L2Update*) override {}
    void on_l3_update(uint32_t symbol_id, const L3Update*) override {}

    void on_timer() override {
        if (g_stop) {
            std::cout << "Benchmark interrupted by user.\n";
            running_ = false;
        }
    }

private:
    void benchmark_loop() {
        while (running_ && !is_ready()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!config_.symbol_ids.empty()) {
            uint32_t symbol_id = config_.symbol_ids[0];
            auto it = symbols_info_.find(symbol_id);
            if (it != symbols_info_.end()) {
                const auto& info = it->second;
                mid_price_ = (info->price_min + info->price_max) / 2;
            }
        }

        start_time_ = std::chrono::steady_clock::now();
        auto last_print_time = start_time_;

        while (running_ && sent_count_.load(std::memory_order_relaxed) < target_requests_) {
            do_trading_action();

            auto now = std::chrono::steady_clock::now();
            if (!silent_ && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_time).count() >= 5000) {
                print_progress();
                last_print_time = now;
            }

            #if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
            #endif
        }

        while (running_) {
            size_t current_received;
            {
                std::lock_guard<std::mutex> lock(stats_mtx_);
                current_received = rtts_all_.size();
            }
            if (current_received >= sent_count_.load(std::memory_order_relaxed)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (sent_count_.load(std::memory_order_relaxed) > 0) {
            report_stats();
        }
        running_ = false;
    }

    std::thread benchmark_thread_;
    uint64_t target_requests_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    bool silent = false;
    uint64_t target_requests = 500000;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" || arg == "--silent") {
            silent = true;
        } else if ((arg == "-r" || arg == "--requests") && i + 1 < argc) {
            target_requests = std::stoull(argv[++i]);
        }
    }

    std::signal(SIGINT, [](int) {
        std::cout << "\n[BenchmarkTrader] Caught SIGINT. Gracefully shutting down..." << std::endl;
        Exchange::g_stop = 1;
    });

    Exchange::AlgoTradingConfig config;
    config.client_id = 1000;
    config.symbol_ids = {1};
    config.timer_interval_ms = 100;

    Exchange::BenchmarkTrader trader(config, target_requests, silent);
    return trader.run();
}
