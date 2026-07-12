#pragma once
#include "client/TradingClientBase.hpp"
#include "client/WSClient.hpp"

namespace Exchange {

class MarketDataClient : virtual public TradingClientBase {
public:
    MarketDataClient(const Config& config);
    virtual ~MarketDataClient();

    virtual void on_l2_update(uint32_t symbol_id, const L2Update* update) { (void) symbol_id; (void) update; }
    virtual void on_l3_update(uint32_t symbol_id, const L3Update* update) { (void) symbol_id; (void) update; }
    virtual void on_l2_batch() {}
    virtual void on_l3_batch() {}

    int run_md();
    int start_md();
    void stop_md();

protected:
    std::unique_ptr<Client::WSClient> md_client_;
    bool md_running_ = true;
};

}
