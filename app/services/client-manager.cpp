#include "LogUtil.hpp"
#include "ClientManager.hpp"
#include "ring/SHMRingBuffer.hpp"
#include "ClientDatabase.hpp"
#include "CSVClientDatabase.hpp"
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
#else
    auto db = std::make_shared<Exchange::CSVClientDatabase>("data/clients.csv");
#endif

    std::unique_ptr<mmaplog::MmapReader> response;
    std::unique_ptr<Exchange::SHMRingBuffer> request;
    try {
        response = std::make_unique<mmaplog::MmapReader>(EXECUTION_JOURNAL_DIR);
        request = std::make_unique<Exchange::SHMRingBuffer>(ORDER_REQUEST, ORDER_REQUEST_SIZE);
    } catch (const std::exception& e) {
        LOG_ERROR("[ClientManager] FATAL: %s", e.what());
        return -1;
    }

    auto ws_adaptor = std::make_shared<Exchange::WSAdaptor>(PORT_CM);
    Exchange::ClientManager manager(ws_adaptor, std::move(request), std::move(response), db);

    int main_core = CM_CORE;
    if (main_core >= 0) {
        Exchange::set_thread_affinity(main_core, "ClientManager");
    }

    manager.run();

    return 0;
}
