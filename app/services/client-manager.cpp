#include "util/LogUtil.hpp"
#include "service/ClientManager.hpp"
#include "ipc/SHMRingBuffer.hpp"
#include "database/common.hpp"
#include "util/ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include "util/SignalHandler.hpp"
#include <iostream>
#include "ipc/mmap_log.h"

int main()
{
    Exchange::initLogger("CM");
    setup_signals();

    auto db = Exchange::DBFactory::createClientDatabase();
    auto sym_db = Exchange::DBFactory::createSymbolDatabase();

    db->start_polling();

    std::unique_ptr<mmaplog::MmapReader> response;
    std::vector<std::unique_ptr<Exchange::SHMRingBuffer>> request_rings;
    try {
        response = std::make_unique<mmaplog::MmapReader>(EXECUTION_JOURNAL_DIR);
        auto cores = sym_db->getAllCores();
        int32_t max_core = -1;
        for (int32_t core_offset : cores) {
            if (core_offset > max_core) max_core = core_offset;
        }
        if (max_core >= 0) {
            request_rings.resize(max_core + 1);
        }
        for (int32_t core_offset : cores) {
            std::string ring_name = ORDER_REQUEST "_" + std::to_string(core_offset);
            request_rings[core_offset] = std::make_unique<Exchange::SHMRingBuffer>(ring_name.c_str(), ORDER_REQUEST_SIZE);
            LOG_INFO("[ClientManager] Initialized request ring %s for core %d", ring_name.c_str(), core_offset);
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

    if (const char* dump_dir = std::getenv("DB_DUMP_DIR")) {
        LOG_INFO("[ClientManager] Dumping DB states to %s", dump_dir);
        db->dump_state(dump_dir);
    }

    return 0;
}
