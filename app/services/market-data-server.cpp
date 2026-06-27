#include "LogUtil.hpp"
#include "MarketDataServer.hpp"
#include "SignalHandler.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include <iostream>

using namespace Exchange;

int main() {
    Exchange::initLogger("MarketDataServer");

    setup_signals();
    
    int main_core = MD_CORE;
    if (main_core >= 0) {
        set_thread_affinity(main_core, "MarketDataServer");
    }

    LOG_INFO("[MarketDataServer] Connecting to Response Ring...");
    mmaplog::MmapReader* response_ring = nullptr;
    try {
        response_ring = new mmaplog::MmapReader(EXECUTION_JOURNAL_DIR);
    } catch (const std::exception& e) {
        LOG_ERROR("[MarketDataServer] FATAL: %d", e.what());
        return -1;
    }

    LOG_INFO("[MarketDataServer] Polling response ring and WebSocket events...");
    MarketDataServer server(PORT_MD, response_ring);
    server.run();

    delete response_ring;
    return 0;
}
