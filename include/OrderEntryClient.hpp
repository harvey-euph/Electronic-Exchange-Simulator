#pragma once
#include "TradingClientBase.hpp"
#include "SimpleWSClient.hpp"
#include "ClientAccount.hpp"
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace Exchange {

class OrderEntryClient : virtual public TradingClientBase {
public:
    OrderEntryClient(const Config& config);
    virtual ~OrderEntryClient();

    virtual void on_order_response(const OrderResponse* response);
    virtual void on_position_response(const PositionResponse* response);

    // Layer 2: Convenience
    void new_limit_order(uint32_t symbol_id, Side side, int64_t p, uint64_t q, uint64_t visible_qty = 0);
    void new_market_order(uint32_t symbol_id, Side side, uint64_t q);

    // Layer 1: Basic Actions
    void new_order(uint32_t symbol_id, Side side, OrderType type, int64_t p, uint64_t q, uint64_t visible_qty = 0);
    void replace_order(uint64_t order_id, int64_t p, uint64_t q, uint32_t symbol_id = 0, Side side = Side_None);
    void cancel_order(uint64_t order_id, uint32_t symbol_id = 0, Side side = Side_None);

    // Layer 0: Raw Request
    virtual void send_order_request(OrderRequestT& order);

    void query_position(uint32_t symbol_id);
    void request_open_orders();
    void request_position(uint32_t symbol_id);

    int run_oe();
    int start_oe();
    void stop_oe();

    bool is_ready() const { return ready_; }
    void wait_until_ready();

protected:
    std::unique_ptr<SimpleWSClient> mgmt_client_;
    bool oe_running_ = true;
    uint64_t next_id_ = 1000001;
    uint64_t o_seq_num_ = 0;
    uint64_t i_seq_num_ = 0;

    std::atomic<bool> ready_{false};
    std::mutex ready_mtx_;
    std::condition_variable ready_cv_;

    ClientAccount account_;
};

}
