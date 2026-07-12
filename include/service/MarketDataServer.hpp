#pragma once

#include "service/MDClient.hpp"
#include "service/WSAdaptor.hpp"
#include "ipc/mmap_log.h"
#include "service/Worker.hpp"
#include "service/MDOrderBook.hpp"
#include "fbs/exchange_generated.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>
#include <atomic>
#include <vector>
#include <optional>
#include <stdexcept>
#include <chrono>
#include <stdexcept>
#include <memory>
#include <atomic>
#include <vector>
#include <optional>
#include <stdexcept>

namespace Exchange {

class MarketDataServer : public Worker<MarketDataServer>
{
public:
    MarketDataServer(std::shared_ptr<WSAdaptor> ws_adaptor, std::unique_ptr<mmaplog::MmapReader> response_ring);
    ~MarketDataServer();

    int poll_client();
    int poll_server();

    void gdb_dump_book(uint32_t symbol_id, const char* filepath);

private:
    std::shared_ptr<MDOrderBook>& get_or_create_book(uint32_t symbol_id);
    void handle_market_data_request(MDClientPtr client, const MarketDataRequest* req);
    void send_top_of_book(MDClientPtr client, uint32_t symbol_id, const std::shared_ptr<MDOrderBook>& book);
    void __send_top_of_book(MDClientPtr client, uint32_t symbol_id, const std::shared_ptr<MDOrderBook>& book);
    void send_l2_snapshot(MDClientPtr client, uint32_t symbol_id, const std::shared_ptr<MDOrderBook>& book);
    void send_l3_snapshot(MDClientPtr client, uint32_t symbol_id, const std::shared_ptr<MDOrderBook>& book);
    void process_market_update(const OrderResponseT* resp);

    std::shared_ptr<WSAdaptor> ws_adaptor_;
    std::unique_ptr<mmaplog::MmapReader> response_ring_;
    
    std::map<uint32_t, std::shared_ptr<MDOrderBook>> books_;
    
    std::unordered_map<uint32_t, std::unordered_set<MDClientPtr>> l1_clients_, l2_clients_, l3_clients_;

    bool crosses(Side side, int64_t price, const std::shared_ptr<MDOrderBook>& book) const;
    void __update(std::shared_ptr<MDOrderBook> book, const OrderResponseT* resp, uint64_t timestamp);
    void publish_l3_update(uint32_t symbol_id, ExecType exec_type, uint64_t order_id, Side side, int64_t p, uint64_t q, uint64_t msg_seq_num, uint64_t timestamp);
    void publish_l2_update(uint32_t symbol_id, const std::vector<L2UpdateT>& updates, uint64_t msg_seq_num, uint64_t timestamp);

    void check_l2_publish_timers();
};

} // namespace Exchange
