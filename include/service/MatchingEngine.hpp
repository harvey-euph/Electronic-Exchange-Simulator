#pragma once

#include "ipc/SHMRingBuffer.hpp"
#include "service/OrderBook.hpp"
#include "service/Worker.hpp"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace Exchange {

class MatchingEngine : public Worker<MatchingEngine> {
public:
    MatchingEngine(SHMRingBuffer* request_ring, std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books);

    int poll_client();
    int poll_server();

private:
    SHMRingBuffer* request_ring_;
    std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books_;
};

} // namespace Exchange
