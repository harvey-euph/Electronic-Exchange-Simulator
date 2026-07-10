#include "client/TradingClientBase.hpp"
#include "client/PublicDataClient.hpp"
#include "util/LogUtil.hpp"

namespace Exchange {

void TradingClientBase::fetch_symbols_info() {
    for (auto symbol_id : config_.symbol_ids) {
        try {
            auto info = PublicDataClient::getSymbolInfo(config_.host, "8081", symbol_id);
            if (info) {
                symbols_info_[symbol_id] = std::move(info);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("[TradingClientBase] Warning: Failed to fetch symbol info for %d: %s", symbol_id, e.what());
        }
    }
}

bool TradingClientBase::validate_price(uint32_t symbol_id, int64_t p, std::string& err_msg) {
    auto it = symbols_info_.find(symbol_id);
    if (it == symbols_info_.end()) {
        err_msg = "Symbol " + std::to_string(symbol_id) + " info not found";
        return false;
    }
    const auto& info = it->second;
    if (p < info->price_min || p > info->price_max) {
        err_msg = "Price " + std::to_string(p) + " out of bounds [" +
                  std::to_string(info->price_min) + ", " + std::to_string(info->price_max) + "]";
        return false;
    }
    if (info->price_min_step > 0 && p % info->price_min_step != 0) {
        err_msg = "Price " + std::to_string(p) + " is not a multiple of step size " +
                  std::to_string(info->price_min_step);
        return false;
    }
    return true;
}

}
