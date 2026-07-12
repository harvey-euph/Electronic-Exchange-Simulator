#include "client/MarketDataClient.hpp"
#include "client/L2Book.hpp"
#include <iostream>

namespace Exchange {

class L2Displayer : public MarketDataClient {
public:
    L2Displayer(const Config& config) : MarketDataClient(config) {
        if (!config_.symbol_ids.empty()) {
            book_.symbol_id = config_.symbol_ids[0];
        }
    }

    void on_l2_update(uint32_t symbol_id, const L2Update* update) override {
        book_.update(update->side(), update->p(), update->q());
    }

    void on_l2_batch() override {
        book_.display();
    }

    // Unused overrides
    void on_l3_update(uint32_t, const L3Update*) override {}

private:
    L2Book book_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L2Displayer client(config);
        return client.run_md();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
