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
    
    void take_snapshot(uint32_t file_index);

private:
    void restore(const std::string& journal_dir);
    void load_snapshot(uint32_t file_index);
    SHMRingBuffer* request_ring_;
    std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books_;
};

} // namespace Exchange
