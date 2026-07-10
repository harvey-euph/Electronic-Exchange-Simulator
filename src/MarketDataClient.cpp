#include "client/MarketDataClient.hpp"
#include "util/LogUtil.hpp"
#include <chrono>
#include <thread>

namespace Exchange {

MarketDataClient::MarketDataClient(const Config& config) : TradingClientBase(config) {
    md_client_ = SimpleWSClient::create(config.host, config.l2_port);
}

MarketDataClient::~MarketDataClient() {
    stop_md();
}

int MarketDataClient::start_md() {
    if (!md_client_->connect()) {
        LOG_ERROR("Failed to connect to Market Data port %s", config_.l2_port.c_str());
        return 1;
    }

    md_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto md_update = flatbuffers::GetRoot<MarketDataUpdate>(data);
        if (md_update->data_type() == MarketDataUpdateData_L2Update) {
            auto update = md_update->data_as_L2Update();
            std::string err;
            if (update->side() != Side_None && update->p() != 0 && !validate_price(update->symbol_id(), update->p(), err)) {
                LOG_ERROR("[MarketDataClient] ERROR: L2 Update has invalid price: %s", err.c_str());
                throw std::runtime_error("L2 Update has invalid price: " + err);
            }
            on_l2_update(update);
        } else if (md_update->data_type() == MarketDataUpdateData_L3Update) {
            auto update = md_update->data_as_L3Update();
            std::string err;
            if (update->side() != Side_None && update->p() != 0 && !validate_price(update->symbol_id(), update->p(), err)) {
                LOG_ERROR("[MarketDataClient] ERROR: L3 Update has invalid price: %s", err.c_str());
                throw std::runtime_error("L3 Update has invalid price: " + err);
            }
            on_l3_update(update);
        }
    });

    for (auto sym : config_.symbol_ids) {
        {
            flatbuffers::FlatBufferBuilder fbb(128);
            auto req = CreateMarketDataRequest(fbb, sym, MDType_L2, SubType_subscribe);
            fbb.Finish(req);
            md_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
        }
        {
            flatbuffers::FlatBufferBuilder fbb(128);
            auto req = CreateMarketDataRequest(fbb, sym, MDType_L3, SubType_subscribe);
            fbb.Finish(req);
            md_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
        }
    }

    return 0;
}

int MarketDataClient::run_md() {
    fetch_symbols_info();
    if (start_md() != 0) return 1;
    while (md_running_) {
        on_timer();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.timer_interval_ms));
    }
    return 0;
}

void MarketDataClient::stop_md() {
    md_running_ = false;
    if (md_client_) md_client_->stop();
}

}
