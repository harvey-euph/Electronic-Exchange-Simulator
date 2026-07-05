#include "LogUtil.hpp"
#include "ClientManager.hpp"
#include "ring/SHMRingBuffer.hpp"
#include "CSVClientDatabase.hpp"
#include "CSVSymbolDatabase.hpp"
#include "DbUtil.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include "SignalHandler.hpp"
#include <iostream>
#include "mmap_log.h"

int main()
{
    Exchange::initLogger("CM");
    setup_signals();

#ifdef USE_PGSQL
    auto db = std::make_shared<Exchange::PostgresClientDatabase>(Exchange::DbUtil::getConnectionString());
    auto sym_db = std::make_shared<Exchange::PostgresSymbolDatabase>(Exchange::DbUtil::getConnectionString());
#else
    auto db = std::make_shared<Exchange::CSVClientDatabase>("data/clients.csv");
    auto sym_db = std::make_shared<Exchange::CSVSymbolDatabase>("data/symbols.csv");
#endif

    std::unique_ptr<mmaplog::MmapReader> response;
    std::map<int32_t, std::unique_ptr<Exchange::SHMRingBuffer>> request_rings;
    try {
        response = std::make_unique<mmaplog::MmapReader>(EXECUTION_JOURNAL_DIR);
        auto cores = sym_db->getAllCores();
        for (int32_t core_id : cores) {
            std::string ring_name = std::string(ORDER_REQUEST) + "_" + std::to_string(core_id);
            request_rings[core_id] = std::make_unique<Exchange::SHMRingBuffer>(ring_name.c_str(), ORDER_REQUEST_SIZE);
            LOG_INFO("[ClientManager] Initialized request ring %s for core %d", ring_name.c_str(), core_id);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[ClientManager] FATAL: %s", e.what());
        return -1;
    }

    auto ws_adaptor = std::make_shared<Exchange::WSAdaptor>(PORT_CM);
    Exchange::ClientManager manager(ws_adaptor, std::move(request_rings), std::move(response), db, sym_db);

    int main_core = CM_CORE;
    if (main_core >= 0) {
        Exchange::set_thread_affinity(main_core, "ClientManager");
    }

    manager.run();

    return 0;
}
