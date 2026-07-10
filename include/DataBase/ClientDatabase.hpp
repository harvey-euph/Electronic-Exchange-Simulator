#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include <memory>
#include "fbs/exchange_generated.h"
#include "define.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include "mmap_log.h"
#include "AffinityConfig.hpp"
#include "ThreadUtil.hpp"

namespace pqxx {
class connection;
}

namespace Exchange {

/**
 * @brief Abstract interface for client data storage.
 * Following the adaptor/interface pattern to allow easy swapping to SQL/other DBs.
 */
class ClientDatabase {
public:
    virtual ~ClientDatabase() {
        stop_polling();
    }

    // TODO: sync for request and polling execution
    virtual void start_polling() {
        if (polling_) return;
        polling_ = true;
        poll_thread_ = std::thread([this]() {
            int db_core = DB_CORE;
            if (db_core >= 0) {
                Exchange::set_thread_affinity(db_core, "ClientDatabase");
            }
            mmaplog::MmapReader response_ring(EXECUTION_JOURNAL_DIR);
            while (polling_.load(std::memory_order_relaxed)) {
                const void* data = nullptr;
                uint32_t len = 0;
                if (response_ring.read_next(data, len)) {
                    if (len >= sizeof(OrderResponseT)) {
                        auto resp = reinterpret_cast<const OrderResponseT*>(data);
                        uint32_t client_id = resp->client_id;
                        uint64_t msg_seq = this->incrementAndGetClientOSeqNum(client_id);
                        this->update_on_execution(resp, msg_seq, true);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    void stop_polling() {
        polling_ = false;
        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
    }

private:
    std::atomic<bool> polling_{false};
    std::thread poll_thread_;

public:

    // Sequence numbers
    virtual uint64_t getClientISeqNum(uint32_t client_id) = 0;
    virtual void setClientISeqNum(uint32_t client_id, uint64_t seq_num) = 0;
    virtual uint64_t getClientOSeqNum(uint32_t client_id) = 0;
    virtual void setClientOSeqNum(uint32_t client_id, uint64_t seq_num) = 0;
    virtual uint64_t incrementAndGetClientOSeqNum(uint32_t client_id) = 0;

    // Unsent OrderResponse lists
    virtual void appendResponseLog(uint32_t client_id, const OrderResponseT& resp, uint64_t msg_seq_num) = 0;
    virtual std::vector<std::vector<uint8_t>> popPendingResponses(uint32_t client_id) = 0;
    virtual std::vector<std::vector<uint8_t>> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) = 0;
    virtual void acknowledgeResponses(uint32_t client_id, uint64_t ack_seq_num) = 0;

    // Positions
    virtual int64_t getPosition(uint32_t client_id, uint32_t symbol_id) = 0;
    virtual std::map<uint32_t, int64_t> getAllPositions(uint32_t client_id) = 0;
    virtual void updatePosition(const OrderResponseT* resp) = 0;

    // Open Orders
    virtual void addOrUpdateOpenOrder(const OrderResponseT* resp) = 0;
    virtual void removeOpenOrder(uint32_t client_id, uint64_t order_id) = 0;
    virtual std::vector<OrderResponseT> getOpenOrders(uint32_t client_id) = 0;

    // Execution processing
    virtual void update_on_execution(const OrderResponseT* resp, uint64_t msg_seq_num, bool not_sent) = 0;
};

} // namespace Exchange

