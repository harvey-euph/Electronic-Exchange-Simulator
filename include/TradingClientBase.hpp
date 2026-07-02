#pragma once
#include "AlgoTradingConfig.hpp"
#include "fbs/exchange_generated.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace Exchange {

class TradingClientBase {
public:
    using Config = AlgoTradingConfig;
    TradingClientBase() = default;
    TradingClientBase(const Config& config) : config_(config) {}
    virtual ~TradingClientBase() = default;

    virtual void on_timer() {}

    void set_timer_interval(uint32_t ms) { config_.timer_interval_ms = ms; }

protected:
    Config config_;
    std::unordered_map<uint32_t, std::unique_ptr<SymbolInfoT>> symbols_info_;

    void fetch_symbols_info();
    virtual bool validate_price(uint32_t symbol_id, int64_t p, std::string& err_msg);
};

}
