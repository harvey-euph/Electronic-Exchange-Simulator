#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include <memory>

namespace Exchange {

// Represents a serialized response (ClientResponse FlatBuffer)
struct PendingResponse {
    std::vector<uint8_t> data;
};

/**
 * @brief Abstract interface for client data storage.
 * Following the adaptor/interface pattern to allow easy swapping to SQL/other DBs.
 */
class ClientDatabase {
public:
    virtual ~ClientDatabase() = default;

    // Unsent OrderResponse lists
    virtual void addPendingResponse(uint32_t client_id, const uint8_t* data, size_t size) = 0;
    virtual std::vector<PendingResponse> popPendingResponses(uint32_t client_id) = 0;

    // Positions
    virtual int64_t getPosition(uint32_t client_id, uint32_t symbol_id) = 0;
    virtual void updatePosition(uint32_t client_id, uint32_t symbol_id, int64_t delta) = 0;
};

/**
 * @brief In-memory implementation of ClientDatabase.
 */
class InMemoryClientDatabase : public ClientDatabase {
public:
    InMemoryClientDatabase() = default;

    void addPendingResponse(uint32_t client_id, const uint8_t* data, size_t size) override {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_responses_[client_id].push_back({std::vector<uint8_t>(data, data + size)});
    }

    std::vector<PendingResponse> popPendingResponses(uint32_t client_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_responses_.find(client_id);
        if (it != pending_responses_.end()) {
            std::vector<PendingResponse> res = std::move(it->second);
            pending_responses_.erase(it);
            return res;
        }
        return {};
    }

    int64_t getPosition(uint32_t client_id, uint32_t symbol_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return get_or_create_position(client_id, symbol_id);
    }

    void updatePosition(uint32_t client_id, uint32_t symbol_id, int64_t delta) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int64_t current = get_or_create_position(client_id, symbol_id);
        positions_[client_id][symbol_id] = current + delta;
    }

private:
    int64_t get_or_create_position(uint32_t client_id, uint32_t symbol_id) {
        auto& client_pos = positions_[client_id];
        auto it = client_pos.find(symbol_id);
        if (it == client_pos.end()) {
            // Default values
            if (symbol_id == 0) return 10000000; // 10M USD
            if (symbol_id == 1) return 10000;    // 10K Symbol 1
            return 0;
        }
        return it->second;
    }

    std::mutex mutex_;
    std::map<uint32_t, std::vector<PendingResponse>> pending_responses_;
    std::map<uint32_t, std::map<uint32_t, int64_t>> positions_;
};

} // namespace Exchange
