#include "LogUtil.hpp"
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
        LOG_ERROR("[PostgresSymbolDatabase] Connection failed: %s", e.what());
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
        LOG_ERROR("[PostgresSymbolDatabase] Query failed: %s", e.what());
        conn_.reset(); // Force reconnect next time
    }
    return false;
}

std::vector<uint32_t> PostgresSymbolDatabase::getSymbolsForCore(int32_t core_offset) {
    std::vector<uint32_t> result;
    try {
        reconnect_if_needed();
        if (!conn_ || !conn_->is_open()) return result;
        pqxx::work w(*conn_);
        pqxx::result r = w.exec(
            "SELECT symbol_id FROM symbols" // Fallback query if core_offset column doesn't exist? Actually let's assume it exists, or just query all and filter if we don't know
        );
        // "由於現在的 symbol 數量不多，讓所有 symbol 都先用同一個 core 運作" -> Just return all for core 0
        if (core_offset == 0) {
            for (auto row : r) {
                result.push_back(row[0].as<uint32_t>());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresSymbolDatabase] Query failed: %s", e.what());
        conn_.reset();
    }
    return result;
}

std::set<int32_t> PostgresSymbolDatabase::getAllCores() {
    std::set<int32_t> result;
    try {
        reconnect_if_needed();
        if (!conn_ || !conn_->is_open()) return result;
        pqxx::work w(*conn_);
        // Assuming core_offset exists in postgres schema. If not, fallback to {0}.
        // But let's just return {0} for now since we said "due to few symbols they all use core 0".
        // A true query would be: pqxx::result r = w.exec("SELECT DISTINCT core_offset FROM symbols");
        result.insert(0);
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresSymbolDatabase] Query failed: %s", e.what());
        conn_.reset();
    }
    return result;
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
