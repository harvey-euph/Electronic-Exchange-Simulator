#include "MatchingEngine.hpp"
#include "OrderBook.hpp"
#include "define.hpp"
#include "TimeUtil.hpp"

namespace Exchange {

// extern thread_local uint64_t g_current_request_start_tsc;

MatchingEngine::MatchingEngine(SHMRingBuffer* request_ring, OrderBook* book)
    : request_ring_(request_ring), book_(book)
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
    book_->processRequest(req);

    request_ring_->release(*slot);
    return 1;
}

} // namespace Exchange
