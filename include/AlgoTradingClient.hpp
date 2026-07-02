#pragma once
#include "OrderEntryClient.hpp"
#include "MarketDataClient.hpp"

namespace Exchange {

class AlgoTradingClient : public OrderEntryClient, public MarketDataClient {
public:
    using Config = AlgoTradingConfig;
    AlgoTradingClient(const Config& config = Config());
    virtual ~AlgoTradingClient();

    int run();
    void stop();

protected:
    bool running_ = true;
};

} // namespace Exchange
