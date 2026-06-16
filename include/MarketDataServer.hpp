#pragma once

#include "WSAdaptor.hpp"
#include "ring/SHMRingBuffer.hpp"
#include "Worker.hpp"
#include "L3Book.hpp"
#include "fbs/exchange_generated.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>
#include <atomic>
#include <vector>

namespace Exchange {

class MarketDataServer : public Worker<MarketDataServer> {
public:
    MarketDataServer(int port, SHMRingBuffer* ring_buffer);
    ~MarketDataServer();

    int poll_client();
    int poll_server();

private:
    std::shared_ptr<L3Book> get_or_create_book(uint32_t symbol_id);
    void setup_handlers();
    void handle_market_data_request(WSClientPtr client, const MarketDataRequest* req);
    void process_market_update(const void* data_ptr, size_t data_size);

    std::shared_ptr<WSAdaptor> ws_adaptor_;
    SHMRingBuffer* ring_buffer_;
    
    std::mutex books_mutex_;
    std::map<uint32_t, std::shared_ptr<L3Book>> books_;

    std::mutex subs_mutex_;
    std::unordered_map<uint32_t, std::unordered_set<WSClientPtr>> l2_subscribers_;
    std::unordered_map<uint32_t, std::unordered_set<WSClientPtr>> l3_subscribers_;
};

} // namespace Exchange
