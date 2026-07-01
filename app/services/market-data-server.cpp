#include "LogUtil.hpp"
#include "MarketDataServer.hpp"
#include "SignalHandler.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include <iostream>

using namespace Exchange;

Exchange::MarketDataServer* g_md_server = nullptr;

int main()
{
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
        LOG_ERROR("[MarketDataServer] FATAL: %s", e.what());
        return -1;
    }

    LOG_INFO("[MarketDataServer] Polling response ring and WebSocket events...");
    auto ws_adaptor = std::make_shared<Exchange::WSAdaptor>(PORT_MD);
    MarketDataServer server(ws_adaptor, response_ring);
    g_md_server = &server;
    server.run();

    if (const char* dump_file = std::getenv("MD_DUMP_FILE")) {
        LOG_INFO("[MarketDataServer] Dumping book to %s", dump_file);
        server.gdb_dump_book(1, dump_file);
    }

    delete response_ring;
    return 0;
}
