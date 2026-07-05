#pragma once

#include "SymbolDatabase.hpp"
#include <string>
#include <unordered_map>

namespace Exchange {

class CSVSymbolDatabase : public SymbolDatabase {
public:
    CSVSymbolDatabase(const std::string& csv_file_path);
    ~CSVSymbolDatabase() override = default;

    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override;

private:
    std::unordered_map<uint32_t, DbSymbolInfo> symbols_;
};

} // namespace Exchange
