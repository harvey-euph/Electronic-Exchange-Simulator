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

/**
 * @brief In-memory implementation of ClientDatabase.
 */
class InMemoryClientDatabase : public ClientDatabase {
public:
    InMemoryClientDatabase() = default;

    uint64_t getClientISeqNum(uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return i_seq_nums_[client_id];
    }

    void setClientISeqNum(uint32_t client_id, uint64_t seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        i_seq_nums_[client_id] = seq_num;
    }

    uint64_t getClientOSeqNum(uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return o_seq_nums_[client_id];
    }

    void setClientOSeqNum(uint32_t client_id, uint64_t seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        o_seq_nums_[client_id] = seq_num;
    }

    uint64_t incrementAndGetClientOSeqNum(uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return ++o_seq_nums_[client_id];
    }

    void appendResponseLog(uint32_t client_id, const OrderResponseT& resp, uint64_t msg_seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        uint64_t o_seq = msg_seq_num;
        flatbuffers::FlatBufferBuilder fbb(256);
        auto resp_offset = OrderResponse::Pack(fbb, &resp);
        auto client_resp = CreateClientResponse(fbb, ClientResponseData_OrderResponse, resp_offset.Union(), msg_seq_num);
        fbb.Finish(client_resp);
        response_log_[client_id][o_seq] = std::vector<uint8_t>(fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize());
    }

    std::vector<std::vector<uint8_t>> popPendingResponses([[maybe_unused]] uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Obsolete, use getResponsesSince
        return {};
    }

    std::vector<std::vector<uint8_t>> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<std::vector<uint8_t>> result;
        auto it = response_log_.find(client_id);
        if (it != response_log_.end()) {
            for (auto const& [seq, resp_bytes] : it->second) {
                if (seq > ack_seq_num) {
                    result.push_back(resp_bytes);
                }
            }
        }
        return result;
    }

    void acknowledgeResponses(uint32_t client_id, uint64_t ack_seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = response_log_.find(client_id);
        if (it != response_log_.end()) {
            auto& log = it->second;
            for (auto it2 = log.begin(); it2 != log.end(); ) {
                if (it2->first <= ack_seq_num) {
                    it2 = log.erase(it2);
                } else {
                    ++it2;
                }
            }
        }
    }

    int64_t getPosition(uint32_t client_id, uint32_t symbol_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto& client_pos = get_or_create_client_positions(client_id);
        return client_pos[symbol_id];
    }

    std::map<uint32_t, int64_t> getAllPositions(uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return get_or_create_client_positions(client_id);
    }

    void updatePosition(const OrderResponseT* resp) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        auto& client_pos = get_or_create_client_positions(client_id);
        client_pos[symbol_id] += asset_delta;
        client_pos[0] += cash_delta;
    }

    void addOrUpdateOpenOrder(const OrderResponseT* resp) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        open_orders_[resp->client_id][resp->order_id] = *resp;
    }

    void removeOpenOrder(uint32_t client_id, uint64_t order_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = open_orders_.find(client_id);
        if (it != open_orders_.end()) {
            it->second.erase(order_id);
        }
    }

    void update_on_execution(const OrderResponseT* resp, uint64_t msg_seq_num, [[maybe_unused]] bool not_sent) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    }

    std::vector<OrderResponseT> getOpenOrders(uint32_t client_id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<OrderResponseT> result;
        auto it = open_orders_.find(client_id);
        if (it != open_orders_.end()) {
            for (auto const& [order_id, resp] : it->second) {
                OrderResponseT order_resp = resp;
                order_resp.exec_type = ExecType_OrderStatus;
                result.push_back(order_resp);
            }
        }
        return result;
    }

protected:
    std::recursive_mutex mutex_;
    std::map<uint32_t, int64_t>& get_or_create_client_positions(uint32_t client_id) {
        auto it = positions_.find(client_id);
        if (it == positions_.end()) {
            auto& client_pos = positions_[client_id];
            client_pos[0] = 1000000; // 10M USD
            client_pos[1] = 0;       //   0 Symbol 1
            return client_pos;
        }
        return it->second;
    }

    std::map<uint32_t, uint64_t> i_seq_nums_;
    std::map<uint32_t, uint64_t> o_seq_nums_;
    std::map<uint32_t, std::map<uint64_t, std::vector<uint8_t>>> response_log_;
    std::map<uint32_t, std::map<uint32_t, int64_t>> positions_;
    std::map<uint32_t, std::map<uint64_t, OrderResponseT>> open_orders_;
};

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

    void update_on_execution(const OrderResponseT* resp, uint64_t msg_seq_num, bool not_sent) override;

private:
    void reconnect_if_needed();

    std::string conn_str_;
    std::unique_ptr<pqxx::connection> conn_;
    std::mutex mutex_;
};

} // namespace Exchange
