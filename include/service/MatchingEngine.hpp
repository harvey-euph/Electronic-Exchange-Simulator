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
    void gdb_dump_book(uint32_t symbol_id, const char* filepath) const;

private:
    void take_snapshot(const std::string& filepath) const;
    void load_snapshot(const std::string& filepath);
    void restore(const std::string& journal_dir);
    void load_snapshot(uint32_t file_index);
    SHMRingBuffer* request_ring_;
    std::unordered_map<uint32_t, std::unique_ptr<OrderBook>> books_;
};

} // namespace Exchange
