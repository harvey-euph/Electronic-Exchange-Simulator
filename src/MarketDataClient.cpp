#include "client/MarketDataClient.hpp"
#include "util/LogUtil.hpp"
#include <chrono>
#include <thread>

namespace Exchange {

MarketDataClient::MarketDataClient(const Config& config) : TradingClientBase(config) {
    md_client_ = Client::WSClient::create(config.host, config.l2_port);
}

MarketDataClient::~MarketDataClient() {
    stop();
}

int MarketDataClient::start() {
    if (!md_client_->connect()) {
        LOG_ERROR("Failed to connect to Market Data port %s", config_.l2_port.c_str());
        return 1;
    }

    md_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto md_update = flatbuffers::GetRoot<MarketDataUpdate>(data);
        if (md_update->data_type() == MarketDataUpdateData_L2Batch) {
            auto batch = md_update->data_as_L2Batch();
            if (!batch->updates()) return;
            for (const auto* update : *batch->updates()) {
                std::string err;
                if (update->side() != Side_None && update->p() != 0 && !validate_price(batch->symbol_id(), update->p(), err)) {
                    LOG_ERROR("[MarketDataClient] ERROR: L2 Batch Update has invalid price: %s", err.c_str());
                    throw std::runtime_error("L2 Batch Update has invalid price: " + err);
                }
                on_l2_update(batch->symbol_id(), update);
            }
            on_l2_batch();
        } else if (md_update->data_type() == MarketDataUpdateData_L3Batch) {
            auto batch = md_update->data_as_L3Batch();
            if (!batch->updates()) return;
            for (const auto* update : *batch->updates()) {
                std::string err;
                if (update->side() != Side_None && update->p() != 0 && !validate_price(batch->symbol_id(), update->p(), err)) {
                    LOG_ERROR("[MarketDataClient] ERROR: L3 Batch Update has invalid price: %s", err.c_str());
                    throw std::runtime_error("L3 Batch Update has invalid price: " + err);
                }
                on_l3_update(batch->symbol_id(), update);
            }
            on_l3_batch();
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

int MarketDataClient::run() {
    fetch_symbols_info();
    if (start() != 0) return 1;
    while (running_) {
        on_timer();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.timer_interval_ms));
    }
    return 0;
}

void MarketDataClient::stop() {
    running_ = false;
    if (md_client_) md_client_->stop();
}

}
