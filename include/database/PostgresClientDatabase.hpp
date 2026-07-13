#pragma once

#include "ClientDatabase.hpp"
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <map>

namespace pqxx {
class connection;
}

namespace Exchange {

class PostgresClientDatabase : public ClientDatabase {
public:
    PostgresClientDatabase(const std::string& conn_str);
    ~PostgresClientDatabase() override;

    void appendResponseLog(uint32_t client_id, const OrderResponseT& resp, uint64_t msg_seq_num) override;
    std::vector<std::vector<uint8_t>> popPendingResponses(uint32_t client_id) override;

    uint64_t getClientISeqNum(uint32_t client_id) override;
    void setClientISeqNum(uint32_t client_id, uint64_t seq_num) override;
    uint64_t getClientOSeqNum(uint32_t client_id) override;
    void setClientOSeqNum(uint32_t client_id, uint64_t seq_num) override;
    uint64_t incrementAndGetClientOSeqNum(uint32_t client_id) override;
    std::vector<std::vector<uint8_t>> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) override;
    void acknowledgeResponses(uint32_t client_id, uint64_t ack_seq_num) override;

    int64_t getPosition(uint32_t client_id, uint32_t symbol_id) override;
    std::map<uint32_t, int64_t> getAllPositions(uint32_t client_id) override;
    void updatePosition(const OrderResponseT* resp) override;

    void addOrUpdateOpenOrder(const OrderResponseT* resp) override;
    void removeOpenOrder(uint32_t client_id, uint64_t order_id) override;
    std::vector<OrderResponseT> getOpenOrders(uint32_t client_id) override;

    void update_on_execution(const OrderResponseT* resp, uint64_t msg_seq_num, bool not_sent, uint64_t log_offset) override;
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
