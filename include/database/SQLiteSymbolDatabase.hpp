#pragma once

#include "SymbolDatabase.hpp"
#include "define.hpp"
#include <string>
#include <memory>
#include <vector>
#include <set>

struct sqlite3;

namespace Exchange {

class SQLiteSymbolDatabase : public SymbolDatabase {
public:
    SQLiteSymbolDatabase(const std::string& db_path);
    ~SQLiteSymbolDatabase() override;

    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override;
    std::vector<uint32_t> getSymbolsForCore(int32_t core_id) override;
    std::set<int32_t> getAllCores() override;

private:
    void init_tables();

    std::string db_path_;
    sqlite3* db_ = nullptr;
};

} // namespace Exchange
