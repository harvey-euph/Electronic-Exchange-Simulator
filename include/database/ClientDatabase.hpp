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
#include "ipc/mmap_log.h"
#include "AffinityConfig.hpp"
#include "util/ThreadUtil.hpp"

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
            uint64_t saved_offset = this->getLastLogOffset();
            if (saved_offset != 0) {
                response_ring.seek(saved_offset);
            }
            while (polling_.load(std::memory_order_relaxed)) {
                const void* data = nullptr;
                uint32_t len = 0;
                if (response_ring.read_next(data, len)) {
                    if (len >= sizeof(OrderResponseT)) {
                        auto resp = reinterpret_cast<const OrderResponseT*>(data);
                        this->update_on_execution(resp, response_ring.get_offset());
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
    std::atomic<uint64_t> last_log_offset_{0};

public:
    virtual uint64_t getLastLogOffset() {
        return last_log_offset_.load(std::memory_order_relaxed);
    }

    virtual void setLastLogOffset(uint64_t offset) {
        last_log_offset_.store(offset, std::memory_order_relaxed);
    }

public:

    // Sequence numbers
    virtual uint64_t getClientISeqNum(uint32_t client_id) = 0;
    virtual void setClientISeqNum(uint32_t client_id, uint64_t seq_num) = 0;
    virtual uint64_t getClientOSeqNum(uint32_t client_id) = 0;
    virtual void setClientOSeqNum(uint32_t client_id, uint64_t seq_num) = 0;
    virtual uint64_t incrementAndGetClientOSeqNum(uint32_t client_id) = 0;

    // Unsent OrderResponse lists
    virtual std::vector<OrderResponseT> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) = 0;

    // Positions
    virtual int64_t getPosition(uint32_t client_id, uint32_t symbol_id) = 0;
    virtual std::map<uint32_t, int64_t> getAllPositions(uint32_t client_id) = 0;

    // Open Orders
    virtual std::vector<OrderResponseT> getOpenOrders(uint32_t client_id) = 0;

    // Execution processing
    virtual void update_on_execution(const OrderResponseT* resp, uint64_t log_offset) = 0;

    // Testing / Debugging
    virtual void dump_state(const std::string& dir) = 0;
};

} // namespace Exchange

