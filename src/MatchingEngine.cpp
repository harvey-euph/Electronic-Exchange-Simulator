#include "service/MatchingEngine.hpp"
#include "service/OrderBook.hpp"
#include "define.hpp"
#include "util/TimeUtil.hpp"
#include "util/LogUtil.hpp"
#include <filesystem>

namespace Exchange {

// extern thread_local uint64_t g_current_request_start_tsc;

MatchingEngine::MatchingEngine(SHMRingBuffer* request_ring, std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books)
    : request_ring_(request_ring), books_(std::move(books))
{
    restore(EXECUTION_JOURNAL_DIR);
}

int MatchingEngine::poll_client()
{
    return 0; // No client network polling needed for Matching Engine
}

int MatchingEngine::poll_server()
{
    auto slot = request_ring_->acquire();
    if (!slot) return 0;

    if (slot->size < sizeof(OrderRequestT)) {
        request_ring_->release(*slot);
        return 0;
    }

    auto req = static_cast<const OrderRequestT*>(slot->payload);
    auto it = books_.find(req->symbol_id);
    if (it != books_.end()) {
        it->second->processRequest(req);
    }

    request_ring_->release(*slot);
    return 1;
}

void MatchingEngine::take_snapshot(uint32_t file_index)
{
    for (const auto& [sid, book] : books_) {
        std::string snapshot_path = std::string(EXECUTION_JOURNAL_DIR) + "/snapshot-" + std::to_string(sid) + "-" + std::to_string(file_index);
        book->take_snapshot(snapshot_path);
        
        if (file_index > 0) {
            std::string prev_snapshot_path = std::string(EXECUTION_JOURNAL_DIR) + "/snapshot-" + std::to_string(sid) + "-" + std::to_string(file_index - 1);
            std::error_code ec;
            std::filesystem::remove(prev_snapshot_path, ec);
        }
    }
}

void MatchingEngine::load_snapshot(uint32_t file_index)
{
    for (auto& [sid, book] : books_) {
        std::string snapshot_path = std::string(EXECUTION_JOURNAL_DIR) + "/snapshot-" + std::to_string(sid) + "-" + std::to_string(file_index);
        book->load_snapshot(snapshot_path);
    }
}

void MatchingEngine::restore(const std::string& journal_dir)
{
    if (!std::filesystem::exists(journal_dir)) {
        LOG_INFO("[MatchingEngine] Journal directory does not exist, skipping restore.");
        return;
    }

    uint32_t max_snapshot_idx = 0;
    bool found_snapshot = false;

    // 1. Find the latest snapshot index
    for (const auto& entry : std::filesystem::directory_iterator(journal_dir)) {
        std::string filename = entry.path().filename().string();
        if (filename.find("snapshot-") == 0) {
            size_t last_dash = filename.find_last_of('-');
            if (last_dash != std::string::npos && last_dash > 8) {
                try {
                    uint32_t idx = std::stoul(filename.substr(last_dash + 1));
                    if (!found_snapshot || idx > max_snapshot_idx) {
                        max_snapshot_idx = idx;
                        found_snapshot = true;
                    }
                } catch (...) {}
            }
        }
    }

    uint32_t start_idx = 0;
    if (found_snapshot) {
        LOG_INFO("[MatchingEngine] Found latest snapshot at index %u, loading...", max_snapshot_idx);
        load_snapshot(max_snapshot_idx);
        start_idx = max_snapshot_idx + 1;
    } else {
        LOG_INFO("[MatchingEngine] No snapshots found, replaying from the beginning.");
    }

    // 2. Replay journals from start_idx
    mmaplog::MmapReader reader(journal_dir);
    // Seek to the beginning of the file (file_index << 32 | offset 0)
    reader.seek((static_cast<uint64_t>(start_idx) << 32));

    const void* data = nullptr;
    uint32_t len = 0;
    uint64_t replay_count = 0;

    while (reader.read_next(data, len)) {
        if (len >= sizeof(OrderResponseT)) {
            const OrderResponseT* resp = reinterpret_cast<const OrderResponseT*>(data);
            auto it = books_.find(resp->symbol_id);
            if (it != books_.end()) {
                it->second->restore_from_response(resp);
                replay_count++;
            }
        }
    }

    LOG_INFO("[MatchingEngine] Recovery complete. Replayed %lu executions.", replay_count);
}

void MatchingEngine::gdb_dump_book(uint32_t symbol_id, const char* filepath) const {
    auto it = books_.find(symbol_id);
    if (it != books_.end()) {
        it->second->dump_raw(filepath);
    }
}

} // namespace Exchange
