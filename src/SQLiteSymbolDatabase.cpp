#include "DataBase/SQLiteSymbolDatabase.hpp"
#include "LogUtil.hpp"
#include "csv_util.hpp"
#include <sqlite3.h>
#include <iostream>

namespace Exchange {

SQLiteSymbolDatabase::SQLiteSymbolDatabase(const std::string& db_path)
    : db_path_(db_path)
{
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR("[SQLiteSymbolDatabase] Connection failed: %s", sqlite3_errmsg(db_));
        throw std::runtime_error("Failed to open SQLite database");
    }
    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    init_tables();
}

SQLiteSymbolDatabase::~SQLiteSymbolDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void SQLiteSymbolDatabase::init_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS symbols (
            symbol_id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            p_exp INTEGER NOT NULL,
            min_step_raw INTEGER NOT NULL,
            min_price_raw INTEGER NOT NULL,
            max_price_raw INTEGER NOT NULL,
            core_offset INTEGER NOT NULL DEFAULT 0
        );
    )";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOG_ERROR("[SQLiteSymbolDatabase] Table init failed: %s", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    // Always use symbols.csv as the source of truth
    LOG_INFO("[SQLiteSymbolDatabase] Loading symbols from data/symbols.csv");
    auto data = readCSV("data/symbols.csv");
    if (!data.empty()) {
        sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "DELETE FROM symbols;", nullptr, nullptr, nullptr);
        
        for (size_t i = 1; i < data.size(); ++i) {
            const auto& row = data[i];
            if (row.size() < 6) continue;
            try {
                uint32_t symbol_id = std::stoul(row[0]);
                std::string name = row[1];
                int p_exp = std::stoi(row[2]);
                int64_t min_step = std::stoll(row[3]);
                int64_t min_price = std::stoll(row[4]);
                int64_t max_price = std::stoll(row[5]);
                int core_offset = (row.size() > 6) ? std::stoi(row[6]) : 0;

                sqlite3_stmt* stmt_ins;
                const char* sql_ins = "INSERT INTO symbols (symbol_id, name, p_exp, min_step_raw, min_price_raw, max_price_raw, core_offset) VALUES (?, ?, ?, ?, ?, ?, ?);";
                if (sqlite3_prepare_v2(db_, sql_ins, -1, &stmt_ins, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmt_ins, 1, symbol_id);
                    sqlite3_bind_text(stmt_ins, 2, name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt_ins, 3, p_exp);
                    sqlite3_bind_int64(stmt_ins, 4, min_step);
                    sqlite3_bind_int64(stmt_ins, 5, min_price);
                    sqlite3_bind_int64(stmt_ins, 6, max_price);
                    sqlite3_bind_int(stmt_ins, 7, core_offset);
                    sqlite3_step(stmt_ins);
                    sqlite3_finalize(stmt_ins);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[SQLiteSymbolDatabase] Failed to parse CSV row %zu: %s", i, e.what());
            }
        }
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    }
}

bool SQLiteSymbolDatabase::getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) {
    if (!db_) return false;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT name, p_exp, min_step_raw, min_price_raw, max_price_raw FROM symbols WHERE symbol_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("[SQLiteSymbolDatabase] Query prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, symbol_id);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.price_exp = sqlite3_column_int(stmt, 1);
        info.min_step = sqlite3_column_int64(stmt, 2);
        info.min_price = sqlite3_column_int64(stmt, 3);
        info.max_price = sqlite3_column_int64(stmt, 4);
        found = true;
    }
    
    sqlite3_finalize(stmt);
    return found;
}

std::vector<uint32_t> SQLiteSymbolDatabase::getSymbolsForCore(int32_t core_offset) {
    std::vector<uint32_t> result;
    if (!db_) return result;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT symbol_id FROM symbols WHERE core_offset = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("[SQLiteSymbolDatabase] Query prepare failed: %s", sqlite3_errmsg(db_));
        return result;
    }
    
    sqlite3_bind_int(stmt, 1, core_offset);
    
    // As per Postgres implementation fallback to all for core 0 if needed, but let's just query correctly here.
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back(sqlite3_column_int(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    
    // If the database is empty, fallback to default behavior?
    // Postgres returns all for core 0 if we assume it.
    if (result.empty() && core_offset == 0) {
        const char* sql_all = "SELECT symbol_id FROM symbols;";
        sqlite3_stmt* stmt_all;
        if (sqlite3_prepare_v2(db_, sql_all, -1, &stmt_all, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt_all) == SQLITE_ROW) {
                result.push_back(sqlite3_column_int(stmt_all, 0));
            }
            sqlite3_finalize(stmt_all);
        }
    }
    
    return result;
}

std::set<int32_t> SQLiteSymbolDatabase::getAllCores() {
    std::set<int32_t> result;
    if (!db_) return result;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT DISTINCT core_offset FROM symbols;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("[SQLiteSymbolDatabase] Query prepare failed: %s", sqlite3_errmsg(db_));
        result.insert(0); // Fallback
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.insert(sqlite3_column_int(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    
    if (result.empty()) {
        result.insert(0);
    }
    return result;
}

} // namespace Exchange
