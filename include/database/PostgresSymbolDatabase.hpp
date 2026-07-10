#pragma once

#include "SymbolDatabase.hpp"
#include <string>
#include <memory>
#include <vector>
#include <set>

namespace pqxx {
class connection;
}

namespace Exchange {

class PostgresSymbolDatabase : public SymbolDatabase {
public:
    PostgresSymbolDatabase(const std::string& conn_str);
    ~PostgresSymbolDatabase() override;
    bool getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) override;
    std::vector<uint32_t> getSymbolsForCore(int32_t core_id) override;
    std::set<int32_t> getAllCores() override;

private:
    void init_tables();
    void reconnect_if_needed();

    std::string conn_str_;
    std::unique_ptr<pqxx::connection> conn_;
};

} // namespace Exchange
