#pragma once

#include "InMemorySymbolDatabase.hpp"
#include "define.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

namespace Exchange {

class CSVSymbolDatabase : public InMemorySymbolDatabase {
public:
    CSVSymbolDatabase(const std::string& csv_file_path) : InMemorySymbolDatabase() {
        (void)csv_file_path;
    }

private:
    std::unordered_map<uint32_t, DbSymbolInfo> symbols_;
};

} // namespace Exchange
