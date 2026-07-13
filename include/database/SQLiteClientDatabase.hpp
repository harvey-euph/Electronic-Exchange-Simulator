#pragma once

#include "define.hpp"
#include "ClientDatabase.hpp"
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <mutex>

struct sqlite3;

namespace Exchange {

class SQLiteClientDatabase : public ClientDatabase {
public:
    SQLiteClientDatabase(const std::string& db_path);
    ~SQLiteClientDatabase() override;



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

protected:
    void init_tables();
    bool execute_sql(const std::string& sql);

    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

} // namespace Exchange
