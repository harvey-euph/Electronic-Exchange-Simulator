#include "client/AlgoTradingClient.hpp"
#include <chrono>
#include <thread>

namespace Exchange {

AlgoTradingClient::AlgoTradingClient(const Config& config) 
    : TradingClientBase(config), OrderEntryClient(config), MarketDataClient(config) {
    this->config_ = config;
}

AlgoTradingClient::~AlgoTradingClient() {
    stop();
}

int AlgoTradingClient::run() {
    fetch_symbols_info();
    if (OrderEntryClient::start() != 0) return 1;
    if (MarketDataClient::start() != 0) return 1;
    
    while (running_) {
        on_timer();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.timer_interval_ms));
    }
    return 0;
}

void AlgoTradingClient::stop() {
    running_ = false;
    OrderEntryClient::stop();
    MarketDataClient::stop();
}

} // namespace Exchange
