#pragma once

#include "CMClient.hpp"
#include "ring/SHMRingBuffer.hpp"
#include "fbs/exchange_generated.h"
#include "mmap_log.h"
#include "WSAdaptor.hpp"
#include "ClientDatabase.hpp"
#include "Worker.hpp"
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace Exchange {

class ClientManager : public Worker<ClientManager>
{
public:
    ClientManager(int port, SHMRingBuffer* request_ring, mmaplog::MmapReader* response_ring, std::shared_ptr<ClientDatabase> db);

    void handle_execution_response(const OrderResponseT* resp);
    void process_client_request(CMClientPtr client, const void* data, size_t size);
    void handle_client_logon(CMClientPtr client, const AdminRequest* admin_req);
    void handle_client_logout(CMClientPtr client);
    
    int poll_client();
    int poll_server();

private:

    std::shared_ptr<WSAdaptor> ws_adaptor_;
    SHMRingBuffer* request_ring_;
    mmaplog::MmapReader* response_ring_;
    std::shared_ptr<ClientDatabase> db_;

    std::mutex clients_mutex_;
    std::map<uint32_t, CMClientPtr> clients_;
};

} // namespace Exchange
