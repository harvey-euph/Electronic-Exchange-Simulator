#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <mutex>
#include <algorithm>
#include "WSAdaptor.hpp"

namespace Exchange {

template <typename Tag>
class EXCHClient {
public:
    explicit EXCHClient(Tag tag) : tag_(tag) {}

    void add_conn(WSClientPtr ws) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (std::find(conns_.begin(), conns_.end(), ws) == conns_.end()) {
            conns_.push_back(ws);
        }
    }

    void remove_conn(WSClientPtr ws) {
        std::lock_guard<std::mutex> lock(mtx_);
        conns_.erase(std::remove(conns_.begin(), conns_.end(), ws), conns_.end());
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return conns_.empty();
    }

    void send(const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& ws : conns_) {
            if (ws) ws->send(data, size);
        }
    }

    Tag tag() const { return tag_; }

    uint32_t client_id() const { return client_id_; }
    void set_client_id(uint32_t id) { client_id_ = id; }

    uint64_t inbound_seq_num() const { return inbound_seq_num_; }
    void set_inbound_seq_num(uint64_t seq) { inbound_seq_num_ = seq; }

    uint64_t outbound_seq_num() const { return outbound_seq_num_; }
    void set_outbound_seq_num(uint64_t seq) { outbound_seq_num_ = seq; }

    std::vector<WSClientPtr> get_conns() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return conns_;
    }

private:
    Tag tag_;
    std::vector<WSClientPtr> conns_;
    mutable std::mutex mtx_;
    
    uint32_t client_id_{0};
    uint64_t inbound_seq_num_{0};
    uint64_t outbound_seq_num_{0};
};

} // namespace Exchange
