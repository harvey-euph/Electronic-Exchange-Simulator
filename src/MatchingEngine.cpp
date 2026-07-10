#include "service/MatchingEngine.hpp"
#include "service/OrderBook.hpp"
#include "define.hpp"
#include "util/TimeUtil.hpp"

namespace Exchange {

// extern thread_local uint64_t g_current_request_start_tsc;

MatchingEngine::MatchingEngine(SHMRingBuffer* request_ring, std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books)
    : request_ring_(request_ring), books_(std::move(books))
{}

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

} // namespace Exchange
