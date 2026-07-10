#pragma once

#include "define.hpp"
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
    int32_t core_offset = 0;
};

class SymbolDatabase {
public:
    virtual ~SymbolDatabase() = default;
    virtual bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) = 0;
    virtual std::vector<uint32_t> getSymbolsForCore(int32_t core_offset) = 0;
    virtual std::set<int32_t> getAllCores() = 0;
};

} // namespace Exchange


