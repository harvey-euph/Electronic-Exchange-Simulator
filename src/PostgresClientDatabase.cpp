#include "util/LogUtil.hpp"
#include "database/PostgresClientDatabase.hpp"
#include "define.hpp"
#include <iostream>
#include <fstream>
#include <pqxx/pqxx>
#include "ipc/mmap_log.h"

namespace Exchange {

PostgresClientDatabase::PostgresClientDatabase(const std::string& conn_str)
    : conn_str_(conn_str)
{
    try {
        conn_ = std::make_unique<pqxx::connection>(conn_str_);
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresClientDatabase] Connection failed: %s", e.what());
        throw;
    }
}

PostgresClientDatabase::~PostgresClientDatabase() = default;

uint64_t PostgresClientDatabase::getClientISeqNum(uint32_t client_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec("SELECT i_seq_num FROM clients WHERE client_id = $1", pqxx::params{client_id});
    if (r.empty()) {
        w.exec("INSERT INTO clients (client_id, username) VALUES ($1, $2) ON CONFLICT DO NOTHING", pqxx::params{client_id, "client_" + std::to_string(client_id)});
        w.commit();
        return 0;
    }
    return r[0][0].as<uint64_t>();
}

void PostgresClientDatabase::setClientISeqNum(uint32_t client_id, uint64_t seq_num) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    w.exec("INSERT INTO clients (client_id, username, i_seq_num) VALUES ($1, $2, $3) ON CONFLICT (client_id) DO UPDATE SET i_seq_num = EXCLUDED.i_seq_num", pqxx::params{client_id, "client_" + std::to_string(client_id), seq_num});
    w.commit();
}

uint64_t PostgresClientDatabase::getClientOSeqNum(uint32_t client_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec("SELECT o_seq_num FROM clients WHERE client_id = $1", pqxx::params{client_id});
    if (r.empty()) {
        w.exec("INSERT INTO clients (client_id, username) VALUES ($1, $2) ON CONFLICT DO NOTHING", pqxx::params{client_id, "client_" + std::to_string(client_id)});
        w.commit();
        return 0;
    }
    return r[0][0].as<uint64_t>();
}

uint64_t PostgresClientDatabase::incrementAndGetClientOSeqNum(uint32_t client_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec("INSERT INTO clients (client_id, username, o_seq_num) VALUES ($1, $2, 1) ON CONFLICT (client_id) DO UPDATE SET o_seq_num = clients.o_seq_num + 1 RETURNING o_seq_num", pqxx::params{client_id, "client_" + std::to_string(client_id)});
    w.commit();
    return r[0][0].as<uint64_t>();
}

void PostgresClientDatabase::setClientOSeqNum(uint32_t client_id, uint64_t seq_num) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    w.exec("INSERT INTO clients (client_id, username, o_seq_num) VALUES ($1, $2, $3) ON CONFLICT (client_id) DO UPDATE SET o_seq_num = EXCLUDED.o_seq_num", pqxx::params{client_id, "client_" + std::to_string(client_id), seq_num});
    w.commit();
}

std::vector<OrderResponseT> PostgresClientDatabase::getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    std::vector<OrderResponseT> result;
    pqxx::work w(*conn_);
    pqxx::result r = w.exec(
        "SELECT o_seq_num, log_offset FROM client_log_offsets WHERE client_id = $1 AND o_seq_num > $2 ORDER BY o_seq_num ASC",
        pqxx::params{client_id, ack_seq_num}
    );
    
    if (!r.empty()) {
        mmaplog::MmapReader reader(EXECUTION_JOURNAL_DIR);
        for (auto const& row : r) {
            uint64_t offset = row[1].as<uint64_t>();
            if (reader.seek(offset)) {
                const void* data = nullptr;
                uint32_t len = 0;
                if (reader.read_next(data, len)) {
                    auto resp = reinterpret_cast<const OrderResponseT*>(data);
                    result.push_back(*resp);
                }
            }
        }
    }
    return result;
}

int64_t PostgresClientDatabase::getPosition(uint32_t client_id, uint32_t symbol_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec(
        "SELECT position FROM positions WHERE client_id = $1 AND symbol_id = $2",
        pqxx::params{client_id, symbol_id}
    );
    if (r.empty()) {
        return (symbol_id == 0) ? 1000000 : 0;
    }
    return r[0][0].as<int64_t>();
}

std::map<uint32_t, int64_t> PostgresClientDatabase::getAllPositions(uint32_t client_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec(
        "SELECT symbol_id, position FROM positions WHERE client_id = $1",
        pqxx::params{client_id}
    );
    std::map<uint32_t, int64_t> result;
    for (auto const& row : r) {
        result[row[0].as<uint32_t>()] = row[1].as<int64_t>();
    }
    if (result.find(0) == result.end()) {
        result[0] = 1000000;
    }
    if (result.find(1) == result.end()) {
        result[1] = 0;
    }
    return result;
}

void PostgresClientDatabase::update_on_execution(const OrderResponseT* resp, uint64_t log_offset) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    
    try {
        pqxx::work w(*conn_);
        uint32_t client_id = resp->client_id;
        std::string username = "client_" + std::to_string(client_id);
        
        w.exec("INSERT INTO clients (client_id, username) VALUES ($1, $2) ON CONFLICT (client_id) DO NOTHING",
               pqxx::params{client_id, username});
        
        pqxx::result r_seq = w.exec("INSERT INTO clients (client_id, username, o_seq_num) VALUES ($1, $2, 1) ON CONFLICT (client_id) DO UPDATE SET o_seq_num = clients.o_seq_num + 1 RETURNING o_seq_num",
                                    pqxx::params{client_id, username});
        uint64_t msg_seq_num = r_seq[0][0].as<uint64_t>();

        if (check_exec(resp->exec_type, EXEC_TRADE)) {
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
            w.exec("INSERT INTO positions (client_id, symbol_id, position) VALUES ($1, 0, 1000000 + $2) ON CONFLICT (client_id, symbol_id) DO UPDATE SET position = positions.position + $3",
                   pqxx::params{client_id, cash_delta, cash_delta});
            if (symbol_id != 0) {
                w.exec("INSERT INTO positions (client_id, symbol_id, position) VALUES ($1, $2, $3) ON CONFLICT (client_id, symbol_id) DO UPDATE SET position = positions.position + $4",
                       pqxx::params{client_id, symbol_id, asset_delta, asset_delta});
            }
        }
        
        if (check_exec(resp->exec_type, EXEC_ALIVE)) {
            uint64_t order_id = (static_cast<uint64_t>(resp->client_id) << 32) | resp->order_id;
            w.exec("INSERT INTO open_orders (order_id, client_id, symbol_id, side, price_mantissa, q, q_rem, timestamp) VALUES ($1, $2, $3, $4, $5, $6, $7, NOW()) ON CONFLICT (order_id) DO UPDATE SET price_mantissa = EXCLUDED.price_mantissa, q = CASE WHEN $8 = 1 THEN open_orders.q ELSE EXCLUDED.q END, q_rem = EXCLUDED.q_rem, updated_at = NOW()",
                   pqxx::params{order_id, client_id, resp->symbol_id, static_cast<int16_t>(resp->side), resp->p, resp->q, resp->q_rem,
                                resp->exec_type == ExecType_PartialFill ? 1 : 0});
        } else if (check_exec(resp->exec_type, EXEC_ANN)) {
            uint64_t combined_order_id = (static_cast<uint64_t>(resp->client_id) << 32) | resp->order_id;
            w.exec("DELETE FROM open_orders WHERE order_id = $1", pqxx::params{combined_order_id});
        }

        w.exec("INSERT INTO client_log_offsets (client_id, o_seq_num, log_offset) VALUES ($1, $2, $3)",
               pqxx::params{client_id, msg_seq_num, log_offset});

        w.exec_params("INSERT INTO system_state (key, value) VALUES ('log_offset', $1) ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value", std::to_string(log_offset));
        w.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresClientDatabase] update_on_execution error: %s", e.what());
    }
}

void PostgresClientDatabase::dump_state(const std::string& dir) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    
    std::string pos_file = dir + "/positions.csv";
    std::string oo_file = dir + "/open-orders.csv";
    
    try {
        pqxx::work w(*conn_);
        
        {
            std::ofstream pos_out(pos_file);
            pqxx::result r = w.exec("SELECT client_id, symbol_id, position FROM positions ORDER BY client_id ASC, symbol_id ASC;");
            for (auto const& row : r) {
                uint32_t cid = row[0].as<uint32_t>();
                uint32_t sid = row[1].as<uint32_t>();
                int64_t pos = row[2].as<int64_t>();
                if (sid != 0 && pos != 0) {
                    pos_out << cid << "," << sid << "," << pos << "\n";
                }
            }
        }
        
        {
            std::ofstream oo_out(oo_file);
            pqxx::result r = w.exec("SELECT order_id, client_id, symbol_id, side, price_mantissa, q, q_rem FROM open_orders ORDER BY client_id ASC, order_id ASC;");
            for (auto const& row : r) {
                uint64_t full_oid = row[0].as<uint64_t>();
                uint32_t cid = row[1].as<uint32_t>();
                uint32_t sid = row[2].as<uint32_t>();
                int16_t side = row[3].as<int16_t>();
                int64_t price = row[4].as<int64_t>();
                int64_t q = row[5].as<int64_t>();
                int64_t q_rem = row[6].as<int64_t>();
                
                uint32_t oid = full_oid & 0xFFFFFFFF;
                std::string side_str = (side == 0) ? "Buy" : "Sell";
                oo_out << cid << "," << oid << "," << sid << "," << side_str << "," << price << "," << q << "," << q_rem << "\n";
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresClientDatabase] Failed to dump state: %s", e.what());
    }
}

std::vector<OrderResponseT> PostgresClientDatabase::getOpenOrders(uint32_t client_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    pqxx::work w(*conn_);
    pqxx::result r = w.exec(
        "SELECT order_id, symbol_id, side, price_mantissa, q, q_rem FROM open_orders WHERE client_id = $1",
        pqxx::params{client_id}
    );
    std::vector<OrderResponseT> result;
    for (auto const& row : r) {
        uint64_t order_id = row[0].as<uint64_t>();
        uint32_t symbol_id = row[1].as<uint32_t>();
        int16_t side = row[2].as<int16_t>();
        int64_t price = row[3].as<int64_t>();
        uint64_t q = row[4].as<uint64_t>();
        uint64_t q_rem = row[5].as<uint64_t>();

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
    return result;
}

void PostgresClientDatabase::reconnect_if_needed() {
    if (!conn_ || !conn_->is_open()) {
        try {
            conn_ = std::make_unique<pqxx::connection>(conn_str_);
        } catch (...) {
            // Ignore
        }
    }
}

} // namespace Exchange

uint64_t PostgresClientDatabase::getLastLogOffset() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    try {
        pqxx::work w(*conn_);
        pqxx::result r = w.exec("SELECT value FROM system_state WHERE key = 'log_offset'");
        if (!r.empty()) {
            return std::stoull(r[0][0].as<std::string>());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresClientDatabase] Error getting offset: %s", e.what());
    }
    return 0;
}

void PostgresClientDatabase::setLastLogOffset(uint64_t offset) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reconnect_if_needed();
    try {
        pqxx::work w(*conn_);
        w.exec_params("INSERT INTO system_state (key, value) VALUES ('log_offset', $1) ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value", std::to_string(offset));
        w.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("[PostgresClientDatabase] Error setting offset: %s", e.what());
    }
}
