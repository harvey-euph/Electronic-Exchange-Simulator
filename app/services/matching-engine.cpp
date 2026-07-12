#include "util/LogUtil.hpp"
#include "service/MatchingEngine.hpp"

#include "util/ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include "util/SignalHandler.hpp"
#include <iostream>
#include "ipc/mmap_log.h"

#include "database/common.hpp"
#include <unordered_map>
#include <memory>
#include <string>

int main(int argc, char* argv[])
{
    int32_t target_core_offset = 0;
    if (argc > 1) {
        target_core_offset = std::stoi(argv[1]);
    }

    std::string logger_name = "ME" + std::to_string(target_core_offset);
    Exchange::initLogger(logger_name.c_str());

    setup_signals();

    int main_core_base = ME_CORE;
    if (main_core_base >= 0) {
        Exchange::set_thread_affinity(main_core_base + target_core_offset, "MatchingEngine");
    }

    LOG_INFO("[OrderCore] Starting matching engine...");

    std::shared_ptr<Exchange::SymbolDatabase> db = Exchange::DBFactory::createSymbolDatabase();

    auto symbol_ids = db->getSymbolsForCore(target_core_offset);
    if (symbol_ids.empty()) {
        LOG_ERROR("[OrderCore] No symbols found for core %d. Shutting down.", target_core_offset);
        return 0;
    }

    LOG_INFO("[OrderCore] Core %d will handle %zu symbols", target_core_offset, symbol_ids.size());

    mmaplog::MmapWriter response_ring(EXECUTION_JOURNAL_DIR);
    
    std::unordered_map<uint32_t, std::unique_ptr<Exchange::OrderBook>> books;
    for (uint32_t sid : symbol_ids) {
        Exchange::DbSymbolInfo info;
        if (db->getSymbolInfo(sid, info)) {
            int64_t min_step = info.min_step;
            int64_t price_offset = info.min_price / info.min_step;
            size_t max_price_levels = (info.max_price - info.min_price) / info.min_step + 1;
            books[sid] = std::make_unique<Exchange::OrderBook>(sid, min_step, price_offset, max_price_levels, &response_ring);
            LOG_INFO("[OrderCore] Created book for symbol %u (%s)", sid, info.name.c_str());
        }
    }

    std::string ring_name = std::string(ORDER_REQUEST) + "_" + std::to_string(target_core_offset);
    Exchange::SHMRingBuffer request_ring(ring_name.c_str(), ORDER_REQUEST_SIZE);

    LOG_INFO("[OrderCore] Listening for requests on %s...", ring_name.c_str());

    Exchange::MatchingEngine engine(&request_ring, std::move(books));

    response_ring.set_rollover_callback([&engine](uint32_t old_idx, uint32_t new_idx) {
        LOG_INFO("[OrderCore] Rollover from %u to %u, taking snapshot...", old_idx, new_idx);
        engine.take_snapshot(old_idx);
    });

    engine.run();

    LOG_INFO("[OrderCore] Shutting down...");
    return 0;
}
