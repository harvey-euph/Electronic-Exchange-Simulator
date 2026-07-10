#pragma once

#include "SymbolDatabase.hpp"
#include "csv_util.hpp"
#include "LogUtil.hpp"
#include <unordered_map>
#include <vector>
#include <set>

namespace Exchange {

class InMemorySymbolDatabase : public SymbolDatabase {
public:
    InMemorySymbolDatabase() {
        auto data = readCSV("data/symbols.csv");
        if (data.empty()) {
            LOG_WARN("InMemorySymbolDatabase: data/symbols.csv is empty or not found. Using defaults.");
            symbols_ = {
                {1, {"BTC", -2, 25, 3000000, 12000000, 0}},
                {2, {"ETH", -2, 10, 150000, 600000, 0}},
                {3, {"SOL", -3, 5, 5000, 500000, 0}}
            };
            return;
        }
        for (size_t i = 1; i < data.size(); ++i) {
            const auto& row = data[i];
            if (row.size() < 6) continue;
            try {
                DbSymbolInfo info;
                uint32_t symbol_id = std::stoul(row[0]);
                info.name = row[1];
                info.price_exp = std::stoi(row[2]);
                info.min_step = std::stoll(row[3]);
                info.min_price = std::stoll(row[4]);
                info.max_price = std::stoll(row[5]);
                info.core_offset = (row.size() > 6) ? std::stoi(row[6]) : 0;
                symbols_[symbol_id] = info;
            } catch (const std::exception& e) {
                LOG_ERROR("InMemorySymbolDatabase: Failed to parse row %zu: %s", i, e.what());
            }
        }
    }

    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override {
        auto it = symbols_.find(symbol_id);
        if (it != symbols_.end()) {
            info = it->second;
            return true;
        }
        return false;
    }
    std::vector<uint32_t> getSymbolsForCore(int32_t core_offset) override {
        std::vector<uint32_t> result;
        for (const auto& [id, info] : symbols_) {
            if (info.core_offset == core_offset) result.push_back(id);
        }
        return result;
    }
    std::set<int32_t> getAllCores() override {
        std::set<int32_t> result;
        for (const auto& [id, info] : symbols_) {
            result.insert(info.core_offset);
        }
        return result;
    }

private:
    std::unordered_map<uint32_t, DbSymbolInfo> symbols_;
};

} // namespace Exchange
