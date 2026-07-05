#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <set>
#include <vector>

namespace pqxx {
class connection;
}

namespace Exchange {

struct DbSymbolInfo {
    std::string name;
    int32_t price_exp = 0;
    int64_t min_step = 0;
    int64_t min_price = 0;
    int64_t max_price = 0;
    int32_t core_id = 1;
};

class SymbolDatabase {
public:
    virtual ~SymbolDatabase() = default;
    virtual bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) = 0;
    virtual std::vector<uint32_t> getSymbolsForCore(int32_t core_id) = 0;
    virtual std::set<int32_t> getAllCores() = 0;
};

class InMemorySymbolDatabase : public SymbolDatabase {
public:
    InMemorySymbolDatabase() = default;
    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override {
        static const std::unordered_map<uint32_t, DbSymbolInfo> internal_symbols = {
            {1, {"BTC", -2, 25, 3000000, 12000000, 1}},
            {2, {"ETH", -2, 10, 150000, 600000, 1}},
            {3, {"SOL", -3, 5, 5000, 500000, 1}}
        };
        auto it = internal_symbols.find(symbol_id);
        if (it != internal_symbols.end()) {
            info = it->second;
            return true;
        }
        return false;
    }
    std::vector<uint32_t> getSymbolsForCore(int32_t core_id) override {
        std::vector<uint32_t> result;
        static const std::unordered_map<uint32_t, DbSymbolInfo> internal_symbols = {
            {1, {"BTC", -2, 25, 3000000, 12000000, 1}},
            {2, {"ETH", -2, 10, 150000, 600000, 1}},
            {3, {"SOL", -3, 5, 5000, 500000, 1}}
        };
        for (const auto& [id, info] : internal_symbols) {
            if (info.core_id == core_id) result.push_back(id);
        }
        return result;
    }
    std::set<int32_t> getAllCores() override {
        std::set<int32_t> result;
        static const std::unordered_map<uint32_t, DbSymbolInfo> internal_symbols = {
            {1, {"BTC", -2, 25, 3000000, 12000000, 1}},
            {2, {"ETH", -2, 10, 150000, 600000, 1}},
            {3, {"SOL", -3, 5, 5000, 500000, 1}}
        };
        for (const auto& [id, info] : internal_symbols) {
            result.insert(info.core_id);
        }
        return result;
    }
};

class PostgresSymbolDatabase : public SymbolDatabase {
public:
    PostgresSymbolDatabase(const std::string& conn_str);
    ~PostgresSymbolDatabase() override;
    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override;
    std::vector<uint32_t> getSymbolsForCore(int32_t core_id) override;
    std::set<int32_t> getAllCores() override;

private:
    void reconnect_if_needed();

    std::string conn_str_;
    std::unique_ptr<pqxx::connection> conn_;
};

} // namespace Exchange

