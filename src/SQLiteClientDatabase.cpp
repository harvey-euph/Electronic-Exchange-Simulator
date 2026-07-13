#include "database/SQLiteClientDatabase.hpp"
#include "util/LogUtil.hpp"
#include "define.hpp"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <vector>

namespace Exchange {

SQLiteClientDatabase::SQLiteClientDatabase(const std::string& db_path)
    : db_path_(db_path)
{
    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR("[SQLiteClientDatabase] Connection failed: %s", sqlite3_errmsg(db_));
        throw std::runtime_error("Failed to open SQLite database");
    }
    
    sqlite3_busy_timeout(db_, 5000);
    
    // Enable foreign keys, WAL mode for better concurrency
    execute_sql("PRAGMA foreign_keys = ON;");
    execute_sql("PRAGMA journal_mode = WAL;");
    execute_sql("PRAGMA synchronous = OFF;");

    init_tables();
}

SQLiteClientDatabase::~SQLiteClientDatabase() {
    if (db_) {
        sqlite3_close(db_);
    }
}

bool SQLiteClientDatabase::execute_sql(const std::string& sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOG_ERROR("[SQLiteClientDatabase] SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

void SQLiteClientDatabase::init_tables() {
    execute_sql(R"(
        CREATE TABLE IF NOT EXISTS system_state (
            key TEXT PRIMARY KEY,
            value TEXT
        );
    )");
    execute_sql(R"(
        CREATE TABLE IF NOT EXISTS clients (
            client_id INTEGER PRIMARY KEY,
            username TEXT NOT NULL,
            i_seq_num INTEGER DEFAULT 0,
            o_seq_num INTEGER DEFAULT 0
        );
    )");
    
    execute_sql(R"(
        CREATE TABLE IF NOT EXISTS pending_responses (
            response_id INTEGER PRIMARY KEY AUTOINCREMENT,
            client_id INTEGER,
            o_seq_num INTEGER,
            exec_id INTEGER,
            serialized_data BLOB,
            FOREIGN KEY(client_id) REFERENCES clients(client_id)
        );
    )");

    execute_sql(R"(
        CREATE TABLE IF NOT EXISTS positions (
            client_id INTEGER,
            symbol_id INTEGER,
            position INTEGER DEFAULT 0,
            PRIMARY KEY(client_id, symbol_id),
            FOREIGN KEY(client_id) REFERENCES clients(client_id)
        );
    )");

    execute_sql(R"(
        CREATE TABLE IF NOT EXISTS open_orders (
            order_id INTEGER PRIMARY KEY,
            client_id INTEGER,
            symbol_id INTEGER,
            side INTEGER,
            price_mantissa INTEGER,
            q INTEGER,
            q_rem INTEGER,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY(client_id) REFERENCES clients(client_id)
        );
    )");
}

void SQLiteClientDatabase::appendResponseLog(uint32_t client_id, const OrderResponseT& resp, uint64_t msg_seq_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t exec_id = resp.exec_id;
    uint64_t o_seq_num = msg_seq_num;
    
    flatbuffers::FlatBufferBuilder fbb(256);
    auto resp_offset = OrderResponse::Pack(fbb, &resp);
    auto client_resp = CreateClientResponse(fbb, ClientResponseData_OrderResponse, resp_offset.Union(), msg_seq_num);
    fbb.Finish(client_resp);

    execute_sql("BEGIN IMMEDIATE TRANSACTION;");
    
    sqlite3_stmt* stmt_client;
    const char* sql_client = "INSERT INTO clients (client_id, username) VALUES (?, ?) ON CONFLICT(client_id) DO NOTHING;";
    if (sqlite3_prepare_v2(db_, sql_client, -1, &stmt_client, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_client, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt_client, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_client);
        sqlite3_finalize(stmt_client);
    }

    sqlite3_stmt* stmt_resp;
    const char* sql_resp = "INSERT INTO pending_responses (client_id, o_seq_num, exec_id, serialized_data) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql_resp, -1, &stmt_resp, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_resp, 1, client_id);
        sqlite3_bind_int64(stmt_resp, 2, o_seq_num);
        sqlite3_bind_int64(stmt_resp, 3, exec_id);
        sqlite3_bind_blob(stmt_resp, 4, fbb.GetBufferPointer(), fbb.GetSize(), SQLITE_TRANSIENT);
        sqlite3_step(stmt_resp);
        sqlite3_finalize(stmt_resp);
    }
    
    execute_sql("COMMIT;");
}

std::vector<std::vector<uint8_t>> SQLiteClientDatabase::popPendingResponses(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::vector<uint8_t>> result;
    
    execute_sql("BEGIN IMMEDIATE TRANSACTION;");
    
    sqlite3_stmt* stmt_select;
    const char* sql_select = "SELECT serialized_data FROM pending_responses WHERE client_id = ? ORDER BY response_id ASC;";
    if (sqlite3_prepare_v2(db_, sql_select, -1, &stmt_select, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_select, 1, client_id);
        while (sqlite3_step(stmt_select) == SQLITE_ROW) {
            const uint8_t* blob = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt_select, 0));
            int bytes = sqlite3_column_bytes(stmt_select, 0);
            result.emplace_back(blob, blob + bytes);
        }
        sqlite3_finalize(stmt_select);
    }
    
    sqlite3_stmt* stmt_delete;
    const char* sql_delete = "DELETE FROM pending_responses WHERE client_id = ?;";
    if (sqlite3_prepare_v2(db_, sql_delete, -1, &stmt_delete, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_delete, 1, client_id);
        sqlite3_step(stmt_delete);
        sqlite3_finalize(stmt_delete);
    }
    
    execute_sql("COMMIT;");
    return result;
}

uint64_t SQLiteClientDatabase::getClientISeqNum(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t seq = 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT i_seq_num FROM clients WHERE client_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            seq = sqlite3_column_int64(stmt, 0);
        } else {
            // Not found, insert
            sqlite3_stmt* stmt_insert;
            const char* sql_insert = "INSERT INTO clients (client_id, username) VALUES (?, ?) ON CONFLICT(client_id) DO NOTHING;";
            if (sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt_insert, 1, client_id);
                std::string username = "client_" + std::to_string(client_id);
                sqlite3_bind_text(stmt_insert, 2, username.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt_insert);
                sqlite3_finalize(stmt_insert);
            }
        }
        sqlite3_finalize(stmt);
    }
    return seq;
}

void SQLiteClientDatabase::setClientISeqNum(uint32_t client_id, uint64_t seq_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO clients (client_id, username, i_seq_num) VALUES (?, ?, ?) ON CONFLICT(client_id) DO UPDATE SET i_seq_num = excluded.i_seq_num;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, seq_num);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

uint64_t SQLiteClientDatabase::getClientOSeqNum(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t seq = 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT o_seq_num FROM clients WHERE client_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            seq = sqlite3_column_int64(stmt, 0);
        } else {
            // Not found, insert
            sqlite3_stmt* stmt_insert;
            const char* sql_insert = "INSERT INTO clients (client_id, username) VALUES (?, ?) ON CONFLICT(client_id) DO NOTHING;";
            if (sqlite3_prepare_v2(db_, sql_insert, -1, &stmt_insert, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt_insert, 1, client_id);
                std::string username = "client_" + std::to_string(client_id);
                sqlite3_bind_text(stmt_insert, 2, username.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt_insert);
                sqlite3_finalize(stmt_insert);
            }
        }
        sqlite3_finalize(stmt);
    }
    return seq;
}

uint64_t SQLiteClientDatabase::incrementAndGetClientOSeqNum(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t seq = 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO clients (client_id, username, o_seq_num) VALUES (?, ?, 1) ON CONFLICT(client_id) DO UPDATE SET o_seq_num = clients.o_seq_num + 1 RETURNING o_seq_num;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            seq = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return seq;
}

void SQLiteClientDatabase::setClientOSeqNum(uint32_t client_id, uint64_t seq_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO clients (client_id, username, o_seq_num) VALUES (?, ?, ?) ON CONFLICT(client_id) DO UPDATE SET o_seq_num = excluded.o_seq_num;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, seq_num);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<std::vector<uint8_t>> SQLiteClientDatabase::getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::vector<uint8_t>> result;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT serialized_data FROM pending_responses WHERE client_id = ? AND o_seq_num > ? ORDER BY o_seq_num ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        sqlite3_bind_int64(stmt, 2, ack_seq_num);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const uint8_t* blob = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
            int bytes = sqlite3_column_bytes(stmt, 0);
            result.emplace_back(blob, blob + bytes);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void SQLiteClientDatabase::acknowledgeResponses(uint32_t client_id, uint64_t ack_seq_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM pending_responses WHERE client_id = ? AND o_seq_num <= ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        sqlite3_bind_int64(stmt, 2, ack_seq_num);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int64_t SQLiteClientDatabase::getPosition(uint32_t client_id, uint32_t symbol_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t pos = (symbol_id == 0) ? 1000000 : 0;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT position FROM positions WHERE client_id = ? AND symbol_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        sqlite3_bind_int(stmt, 2, symbol_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pos = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return pos;
}

std::map<uint32_t, int64_t> SQLiteClientDatabase::getAllPositions(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<uint32_t, int64_t> result;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT symbol_id, position FROM positions WHERE client_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result[sqlite3_column_int(stmt, 0)] = sqlite3_column_int64(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    
    if (result.find(0) == result.end()) {
        result[0] = 1000000;
    }
    if (result.find(1) == result.end()) {
        result[1] = 0;
    }
    return result;
}

void SQLiteClientDatabase::updatePosition(const OrderResponseT* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t client_id = resp->client_id;
    uint32_t symbol_id = resp->symbol_id;
    int64_t cost = static_cast<int64_t>(resp->p * resp->q);
    int64_t asset_delta = 0;
    int64_t cash_delta = 0;
    
    if (resp->side == Side_Buy) {
        asset_delta = static_cast<int64_t>(resp->q);
        cash_delta = -cost;
    } else {
        asset_delta = -static_cast<int64_t>(resp->q);
        cash_delta = cost;
    }

    execute_sql("BEGIN IMMEDIATE TRANSACTION;");
    
    // Ensure client exists
    sqlite3_stmt* stmt_client;
    const char* sql_client = "INSERT INTO clients (client_id, username) VALUES (?, ?) ON CONFLICT(client_id) DO NOTHING;";
    if (sqlite3_prepare_v2(db_, sql_client, -1, &stmt_client, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_client, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt_client, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_client);
        sqlite3_finalize(stmt_client);
    }

    // Cash position
    sqlite3_stmt* stmt_cash;
    const char* sql_cash = "INSERT INTO positions (client_id, symbol_id, position) VALUES (?, 0, 1000000 + ?) ON CONFLICT(client_id, symbol_id) DO UPDATE SET position = positions.position + ?;";
    if (sqlite3_prepare_v2(db_, sql_cash, -1, &stmt_cash, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_cash, 1, client_id);
        sqlite3_bind_int64(stmt_cash, 2, cash_delta);
        sqlite3_bind_int64(stmt_cash, 3, cash_delta);
        sqlite3_step(stmt_cash);
        sqlite3_finalize(stmt_cash);
    }

    // Asset position
    if (symbol_id != 0) {
        sqlite3_stmt* stmt_asset;
        const char* sql_asset = "INSERT INTO positions (client_id, symbol_id, position) VALUES (?, ?, ?) ON CONFLICT(client_id, symbol_id) DO UPDATE SET position = positions.position + ?;";
        if (sqlite3_prepare_v2(db_, sql_asset, -1, &stmt_asset, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt_asset, 1, client_id);
            sqlite3_bind_int(stmt_asset, 2, symbol_id);
            sqlite3_bind_int64(stmt_asset, 3, asset_delta);
            sqlite3_bind_int64(stmt_asset, 4, asset_delta);
            sqlite3_step(stmt_asset);
            sqlite3_finalize(stmt_asset);
        }
    }
    
    execute_sql("COMMIT;");
}

void SQLiteClientDatabase::update_on_execution(const OrderResponseT* resp, uint64_t msg_seq_num, [[maybe_unused]] bool not_sent, uint64_t log_offset) {
    uint32_t client_id = resp->client_id;
    if (check_exec(resp->exec_type, EXEC_TRADE)) {
        updatePosition(resp);
    }
    if (check_exec(resp->exec_type, EXEC_ALIVE)) {
        addOrUpdateOpenOrder(resp);
    } else if (check_exec(resp->exec_type, EXEC_ANN)) {
        removeOpenOrder(client_id, resp->order_id);
    }
    appendResponseLog(client_id, *resp, msg_seq_num);
    setLastLogOffset(log_offset);
}

void SQLiteClientDatabase::addOrUpdateOpenOrder(const OrderResponseT* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t client_id = resp->client_id;
    uint64_t order_id = (static_cast<uint64_t>(resp->client_id) << 32) | resp->order_id;
    uint32_t symbol_id = resp->symbol_id;
    int16_t side = static_cast<int16_t>(resp->side);
    int64_t price = resp->p;
    uint64_t qty = resp->q;

    execute_sql("BEGIN IMMEDIATE TRANSACTION;");
    
    sqlite3_stmt* stmt_client;
    const char* sql_client = "INSERT INTO clients (client_id, username) VALUES (?, ?) ON CONFLICT(client_id) DO NOTHING;";
    if (sqlite3_prepare_v2(db_, sql_client, -1, &stmt_client, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_client, 1, client_id);
        std::string username = "client_" + std::to_string(client_id);
        sqlite3_bind_text(stmt_client, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt_client);
        sqlite3_finalize(stmt_client);
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO open_orders (order_id, client_id, symbol_id, side, price_mantissa, q, q_rem, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
                      "ON CONFLICT(order_id) DO UPDATE SET "
                      "price_mantissa = excluded.price_mantissa, "
                      "q = CASE WHEN ? = 1 THEN excluded.q ELSE open_orders.q END, "
                      "q_rem = excluded.q_rem, "
                      "updated_at = CURRENT_TIMESTAMP;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, order_id);
        sqlite3_bind_int(stmt, 2, client_id);
        sqlite3_bind_int(stmt, 3, symbol_id);
        sqlite3_bind_int(stmt, 4, side);
        sqlite3_bind_int64(stmt, 5, price);
        sqlite3_bind_int64(stmt, 6, resp->q);
        sqlite3_bind_int64(stmt, 7, resp->q_rem);
        bool is_state_update = (resp->exec_type == ExecType_New || resp->exec_type == ExecType_Replaced);
        sqlite3_bind_int(stmt, 8, is_state_update ? 1 : 0);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            LOG_ERROR("[SQLiteClientDatabase] addOrUpdateOpenOrder error: %s", sqlite3_errmsg(db_));
        }
        sqlite3_finalize(stmt);
    } else {
        LOG_ERROR("[SQLiteClientDatabase] addOrUpdateOpenOrder prepare error: %s", sqlite3_errmsg(db_));
    }
    
    execute_sql("COMMIT;");
}

void SQLiteClientDatabase::removeOpenOrder(uint32_t client_id, uint64_t order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t combined_order_id = (static_cast<uint64_t>(client_id) << 32) | order_id;
    
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM open_orders WHERE order_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, combined_order_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<OrderResponseT> SQLiteClientDatabase::getOpenOrders(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OrderResponseT> result;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT order_id, symbol_id, side, price_mantissa, q, q_rem FROM open_orders WHERE client_id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, client_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            uint64_t order_id = sqlite3_column_int64(stmt, 0);
            uint32_t symbol_id = sqlite3_column_int(stmt, 1);
            int16_t side = sqlite3_column_int(stmt, 2);
            int64_t price = sqlite3_column_int64(stmt, 3);
            uint64_t q = sqlite3_column_int64(stmt, 4);
            uint64_t q_rem = sqlite3_column_int64(stmt, 5);

            OrderResponseT resp;
            resp.exec_type = ExecType_OrderStatus;
            resp.order_id = static_cast<uint32_t>(order_id & 0xFFFFFFFF);
            resp.client_id = client_id;
            resp.exec_id = 0;
            resp.symbol_id = symbol_id;
            resp.side = static_cast<Side>(side);
            resp.p = price;
            resp.q = q;
            resp.q_rem = q_rem;
            resp.reject_code = RejectCode_None;

            result.push_back(resp);
        }
        sqlite3_finalize(stmt);
    }
    
    return result;
}


uint64_t SQLiteClientDatabase::getLastLogOffset() {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t offset = 0;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT value FROM system_state WHERE key = 'log_offset';";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            offset = std::stoull(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }
    return offset;
}

void SQLiteClientDatabase::setLastLogOffset(uint64_t offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO system_state (key, value) VALUES ('log_offset', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string val = std::to_string(offset);
        sqlite3_bind_text(stmt, 1, val.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void SQLiteClientDatabase::dump_state(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string pos_file = dir + "/positions.csv";
    std::string oo_file = dir + "/open-orders.csv";
    
    {
        std::ofstream pos_out(pos_file);
        sqlite3_stmt* stmt;
        const char* sql = "SELECT client_id, symbol_id, position FROM positions ORDER BY client_id ASC, symbol_id ASC;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                uint32_t cid = sqlite3_column_int(stmt, 0);
                uint32_t sid = sqlite3_column_int(stmt, 1);
                int64_t pos = sqlite3_column_int64(stmt, 2);
                if (sid != 0 && pos != 0) {
                    pos_out << cid << "," << sid << "," << pos << "\n";
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    
    {
        std::ofstream oo_out(oo_file);
        sqlite3_stmt* stmt;
        const char* sql = "SELECT order_id, client_id, symbol_id, side, price_mantissa, q, q_rem FROM open_orders ORDER BY client_id ASC, order_id ASC;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                uint64_t full_oid = sqlite3_column_int64(stmt, 0);
                uint32_t cid = sqlite3_column_int(stmt, 1);
                uint32_t sid = sqlite3_column_int(stmt, 2);
                int16_t side = sqlite3_column_int(stmt, 3);
                int64_t price = sqlite3_column_int64(stmt, 4);
                int64_t q = sqlite3_column_int64(stmt, 5);
                int64_t q_rem = sqlite3_column_int64(stmt, 6);
                
                uint32_t oid = full_oid & 0xFFFFFFFF;
                std::string side_str = (side == 0) ? "Buy" : "Sell"; // Side_Buy = 0, Side_Sell = 1
                oo_out << cid << "," << oid << "," << sid << "," << side_str << "," << price << "," << q << "," << q_rem << "\n";
            }
            sqlite3_finalize(stmt);
        }
    }
}

} // namespace Exchange
