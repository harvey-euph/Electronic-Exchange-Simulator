#include "client/AlgoTradingClient.hpp"
#include "util/LogUtil.hpp"
#include <iostream>

namespace Exchange {

class StdoutClient : public AlgoTradingClient {
public:
    using AlgoTradingClient::AlgoTradingClient;

    void on_l2_update(uint32_t symbol_id, const L2Update* update) override {
        logL2Update(symbol_id, update, "[StdoutClient]");
    }

    void on_l3_update(uint32_t symbol_id, const L3Update* update) override {
        logL3Update(symbol_id, update, "[StdoutClient]");
    }

    void on_order_response(const OrderResponse* response) override {
        logOrderResponse(response, "[StdoutClient]");
    }

    void on_position_response(const PositionResponse* response) override {
        logPositionResponse(response, "[StdoutClient]");
    }
};

} // namespace Exchange

int main() {
    Exchange::StdoutClient cli1;
    return cli1.run();
}
