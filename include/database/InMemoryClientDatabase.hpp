#pragma once

#include "ClientDatabase.hpp"
#include <mutex>
#include <map>
#include <vector>

namespace Exchange {

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

    std::vector<OrderResponseT> getResponsesSince(uint32_t client_id, uint64_t ack_seq_num) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<OrderResponseT> result;
        auto it = log_offsets_.find(client_id);
        if (it != log_offsets_.end()) {
            mmaplog::MmapReader reader(EXECUTION_JOURNAL_DIR);
            for (auto const& [seq, offset] : it->second) {
                if (seq > ack_seq_num) {
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
        }
        return result;
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

    void update_on_execution(const OrderResponseT* resp, uint64_t log_offset) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        uint32_t client_id = resp->client_id;
        uint32_t symbol_id = resp->symbol_id;
        
        uint64_t msg_seq_num = ++o_seq_nums_[client_id];

        if (check_exec(resp->exec_type, EXEC_TRADE)) {
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
        
        if (check_exec(resp->exec_type, EXEC_ALIVE)) {
            if (resp->exec_type == ExecType_PartialFill) {
                auto it = open_orders_[client_id].find(resp->order_id);
                if (it != open_orders_[client_id].end()) {
                    it->second.q_rem = resp->q_rem;
                }
            } else {
                open_orders_[client_id][resp->order_id] = *resp;
            }
        } else if (check_exec(resp->exec_type, EXEC_ANN)) {
            auto it = open_orders_.find(client_id);
            if (it != open_orders_.end()) {
                it->second.erase(resp->order_id);
            }
        }
        
        log_offsets_[client_id][msg_seq_num] = log_offset;
        setLastLogOffset(log_offset);
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

    void dump_state(const std::string& dir) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::string pos_file = dir + "/positions.csv";
        std::string oo_file = dir + "/open-orders.csv";
        
        std::ofstream pos_out(pos_file);
        for (auto const& [client_id, pos_map] : positions_) {
            for (auto const& [symbol_id, qty] : pos_map) {
                if (symbol_id != 0) {
                    pos_out << client_id << "," << symbol_id << "," << qty << "\n";
                }
            }
        }
        
        std::ofstream oo_out(oo_file);
        for (auto const& [client_id, orders] : open_orders_) {
            for (auto const& [order_id, resp] : orders) {
                oo_out << client_id << "," << resp.order_id << "," << resp.symbol_id << "," 
                       << EnumNameSide(resp.side) << "," << resp.p << "," << resp.q << "," << resp.q_rem << "\n";
            }
        }
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
    std::map<uint32_t, std::map<uint64_t, uint64_t>> log_offsets_;
    std::map<uint32_t, std::map<uint32_t, int64_t>> positions_;
    std::map<uint32_t, std::map<uint64_t, OrderResponseT>> open_orders_;
};

} // namespace Exchange
