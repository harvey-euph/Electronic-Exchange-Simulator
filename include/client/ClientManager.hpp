#pragma once

#include "service/CMClient.hpp"
#include "ipc/SHMRingBuffer.hpp"
#include "fbs/exchange_generated.h"
#include "ipc/mmap_log.h"
#include "service/WSAdaptor.hpp"
#include "database/common.hpp"
#include "service/Worker.hpp"
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>

// SymbolDatabase is included via DataBase/common.hpp

#include <queue>

namespace Exchange {

class ClientManager : public Worker<ClientManager>
{
public:
    ClientManager(std::shared_ptr<WSAdaptor> ws_adaptor, 
                  std::vector<std::unique_ptr<SHMRingBuffer>> request_rings, 
                  std::unique_ptr<mmaplog::MmapReader> response_ring, 
                  std::shared_ptr<ClientDatabase> db,
                  std::shared_ptr<SymbolDatabase> sym_db);

    void handle_execution_response(const OrderResponseT* resp);
    void process_client_request(CMClientPtr client, const void* data, size_t size);
    void handle_client_logon(CMClientPtr client, const AdminRequest* admin_req, uint64_t msg_seq_num);
    void handle_client_logout(CMClientPtr client);
    
    int poll_client();
    int poll_server();

private:

    std::shared_ptr<WSAdaptor> ws_adaptor_;
    std::vector<std::unique_ptr<SHMRingBuffer>> request_rings_;
    std::unique_ptr<mmaplog::MmapReader> response_ring_;
    std::shared_ptr<ClientDatabase> db_;
    std::shared_ptr<SymbolDatabase> sym_db_;

    std::vector<std::queue<CMClientPtr>> pending_order_clients_;
    CMClientPtr last_popped_client_;

    std::mutex clients_mutex_;
    std::map<uint32_t, CMClientPtr> clients_;
};

} // namespace Exchange
