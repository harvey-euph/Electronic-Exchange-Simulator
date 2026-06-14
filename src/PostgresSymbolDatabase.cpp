#include "SymbolDatabase.hpp"
#include <pqxx/pqxx>
#include <iostream>

namespace Exchange {

PostgresSymbolDatabase::PostgresSymbolDatabase(const std::string& conn_str)
    : conn_str_(conn_str)
{
    try {
        conn_ = std::make_unique<pqxx::connection>(conn_str_);
    } catch (const std::exception& e) {
        std::cerr << "[PostgresSymbolDatabase] Connection failed: " << e.what() << std::endl;
        throw;
    }
}

PostgresSymbolDatabase::~PostgresSymbolDatabase() = default;

bool PostgresSymbolDatabase::getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) {
    try {
        reconnect_if_needed();
        if (!conn_ || !conn_->is_open()) {
            return false;
        }
        pqxx::work w(*conn_);
        pqxx::result r = w.exec(
            "SELECT name, p_exp, min_step_raw, min_price_raw, max_price_raw FROM symbols WHERE symbol_id = $1",
            pqxx::params{symbol_id}
        );
        if (!r.empty()) {
            info.name = r[0][0].as<std::string>();
            info.price_exp = r[0][1].as<int32_t>();
            info.min_step = r[0][2].as<int64_t>();
            info.min_price = r[0][3].as<int64_t>();
            info.max_price = r[0][4].as<int64_t>();
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[PostgresSymbolDatabase] Query failed: " << e.what() << std::endl;
        conn_.reset(); // Force reconnect next time
    }
    return false;
}

void PostgresSymbolDatabase::reconnect_if_needed() {
    if (!conn_ || !conn_->is_open()) {
        try {
            conn_ = std::make_unique<pqxx::connection>(conn_str_);
        } catch (...) {
            // Ignore
        }
    }
}

} // namespace Exchange
