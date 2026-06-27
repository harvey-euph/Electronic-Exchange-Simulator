#include "LogUtil.hpp"
#include "MatchingEngine.hpp"
#include "DbUtil.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include "SignalHandler.hpp"
#include <iostream>
#include "mmap_log.h"


int main() {
    Exchange::initLogger("MatchingEngine");
    LOG_INFO("================================================================================");

    setup_signals();

    int main_core = ME_CORE;
    if (main_core >= 0) {
        Exchange::set_thread_affinity(main_core, "MatchingEngine");
    }

    LOG_INFO("[OrderCore] Starting matching engine...");

    // Query DB for symbol 1 parameters
    int64_t min_step = 25;                       // min_step_raw for BTC
    int64_t price_offset = 120000;              // min_price_raw / min_step_raw (3000000 / 25)
    size_t max_price_levels = 360001;           // (max_price_raw - min_price_raw) / min_step_raw + 1
    
    LOG_INFO("[OrderCore] Using internal Symbol 1 config (BTC): min_step=%d, price_offset=%d, max_price_levels=%d", min_step, price_offset, max_price_levels);

    /*
    try {
        auto conn = Exchange::DbUtil::getDbConnection();
        pqxx::work w(*conn);
        pqxx::result r = w.exec(
            "SELECT min_step_raw, min_price_raw, max_price_raw FROM symbols WHERE symbol_id = 1"
        );
        if (!r.empty()) {
            int64_t min_step_raw = r[0][0].as<int64_t>();
            int64_t min_price_raw = r[0][1].as<int64_t>();
            int64_t max_price_raw = r[0][2].as<int64_t>();
            
            min_step = min_step_raw;
            price_offset = min_price_raw / min_step_raw;
            max_price_levels = (max_price_raw - min_price_raw) / min_step_raw + 1;
            LOG_INFO("[OrderCore] Loaded Symbol 1 config from DB: min_step=%d, price_offset=%d, max_price_levels=%d", min_step, price_offset, max_price_levels);
        } else {
            LOG_ERROR("[OrderCore] WARNING: Symbol 1 not found in DB, using default parameters");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[OrderCore] ERROR querying DB: %d, using default parameters", e.what());
    }
    */

    mmaplog::MmapWriter response_ring(EXECUTION_JOURNAL_DIR);
    Exchange::OrderBook book(1, min_step, price_offset, max_price_levels, &response_ring);

    Exchange::SHMRingBuffer request_ring(ORDER_REQUEST, ORDER_REQUEST_SIZE);

    LOG_INFO("[OrderCore] Listening for requests on OrderRequest ring...");

    Exchange::MatchingEngine engine(&request_ring, &book);
    engine.run();

    LOG_INFO("[OrderCore] Shutting down...");
    return 0;
}
