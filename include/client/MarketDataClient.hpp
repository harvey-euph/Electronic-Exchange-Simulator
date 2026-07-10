#pragma once
#include "client/TradingClientBase.hpp"
#include "client/SimpleWSClient.hpp"

namespace Exchange {

class MarketDataClient : virtual public TradingClientBase {
public:
    MarketDataClient(const Config& config);
    virtual ~MarketDataClient();

    virtual void on_l2_update(const L2Update* update) = 0;
    virtual void on_l3_update(const L3Update* update) = 0;

    int run_md();
    int start_md();
    void stop_md();

protected:
    std::unique_ptr<SimpleWSClient> md_client_;
    bool md_running_ = true;
};

}
