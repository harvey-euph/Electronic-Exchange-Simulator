#include "client/MarketDataClient.hpp"
#include "client/L3Book.hpp"
#include <iostream>

namespace Exchange {

class L3Displayer : public MarketDataClient {
public:
    L3Displayer(const Config& config) : MarketDataClient(config) {
        if (!config_.symbol_ids.empty()) {
            book_.symbol_id = config_.symbol_ids[0];
        }
    }

    void on_l3_update(uint32_t symbol_id, const L3Update* update) override {
        book_.update(update->exec_type(), update->order_id(), update->side(), update->p(), update->q());
    }

    void on_l3_batch() override {
        book_.display();
    }

    // Unused overrides
    void on_l2_update(uint32_t, const L2Update*) override {}

private:
    L3Book book_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L3Displayer client(config);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
