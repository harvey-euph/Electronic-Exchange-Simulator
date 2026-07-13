#pragma once

#include "ClientDatabase.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <map>

#include <pqxx/pqxx>
#include <cstdlib>
#include <iostream>
#include "util/env-util.hpp"

namespace Exchange {

namespace DbUtil {

inline std::string getConnectionString() {
    Exchange::Util::loadEnvFile();
    const char* env_conn = std::getenv("DATABASE_URL");
    if (env_conn) {
        return env_conn;
    }
    // Fallback to default peer authentication for local exchange db
    return "dbname=exchange";
}

inline std::unique_ptr<pqxx::connection> getDbConnection() {
    try {
        return std::make_unique<pqxx::connection>(getConnectionString());
    } catch (const std::exception& e) {
        std::cerr << "[DbUtil] Database connection failed: " << e.what() << std::endl;
        throw;
    }
}

} // namespace DbUtil

class PostgresClientDatabase : public ClientDatabase {
public:
    PostgresClientDatabase(const std::string& conn_str);
    ~PostgresClientDatabase() override;


    uint64_t getClientISeqNum(uint32_t client_id) override;
    void setClientISeqNum(uint32_t client_id, uint64_t seq_num) override;
    uint64_t getClientOSeqNum(uint32_t client_id) override;
    void setClientOSeqNum(uint32_t client_id, uint64_t seq_num) override;
    uint64_t incrementAndGetClientOSeqNum(uint32_t client_id) override;
    std::vector<OrderResponseT> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) override;

    int64_t getPosition(uint32_t client_id, uint32_t symbol_id) override;
    std::map<uint32_t, int64_t> getAllPositions(uint32_t client_id) override;
    void update_on_execution(const OrderResponseT* resp, uint64_t log_offset) override;
    std::vector<OrderResponseT> getOpenOrders(uint32_t client_id) override;
    uint64_t getLastLogOffset() override;
    void setLastLogOffset(uint64_t offset) override;

    void dump_state(const std::string& dir) override;
private:
    void reconnect_if_needed();

    std::string conn_str_;
    std::unique_ptr<pqxx::connection> conn_;
    std::recursive_mutex mutex_;
};

} // namespace Exchange
