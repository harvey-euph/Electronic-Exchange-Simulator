#include "MatchingEngine.hpp"
#include "TimeUtil.hpp"

namespace Exchange {

// extern thread_local uint64_t g_current_request_start_tsc;

MatchingEngine::MatchingEngine(SHMRingBuffer* request_ring, OrderBook* book)
    : request_ring_(request_ring), book_(book)
{}

int MatchingEngine::poll_client() {
    return 0; // No client network polling needed for Matching Engine
}

int MatchingEngine::poll_server() {
    void* data_ptr = nullptr;
    size_t data_size = 0;
    if (request_ring_->dequeue(&data_ptr, &data_size)) {
        if (!data_ptr || data_size != sizeof(uint32_t)) return 0;

        uint32_t req_index = *reinterpret_cast<uint32_t*>(data_ptr);
        auto req = book_->req_pool_.get_pointer(req_index);
        book_->processRequest(req);

        // g_current_request_start_tsc = 0;
        return 1;
    }
    return 0;
}

} // namespace Exchange
