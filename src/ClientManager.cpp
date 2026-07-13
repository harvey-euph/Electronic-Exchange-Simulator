#include "service/ClientManager.hpp"
#include "util/LogUtil.hpp"
#include "util/TimeUtil.hpp"
#include <iostream>
#include <algorithm>
#include <new>
#include <sys/sdt.h>
#include "define.hpp"

namespace Exchange {

ClientManager::ClientManager(std::shared_ptr<WSAdaptor> ws_adaptor, 
                             std::vector<std::unique_ptr<SHMRingBuffer>> request_rings, 
                             std::unique_ptr<mmaplog::MmapReader> response_ring, 
                             std::shared_ptr<ClientDatabase> db,
                             std::shared_ptr<SymbolDatabase> sym_db) 
    : ws_adaptor_(ws_adaptor)
    , request_rings_(std::move(request_rings))
    , response_ring_(std::move(response_ring))
    , db_(db)
    , sym_db_(sym_db)
{
    LOG_INFO("[ClientManager] Initializing");
    pending_order_clients_.resize(request_rings_.size());

    CMClient::bind_adaptor(
        ws_adaptor_,
        nullptr,
        [this](CMClientPtr client) {
            this->handle_client_logout(client);
        },
        [this](CMClientPtr client, const void* data, size_t size) {
            this->process_client_request(client, data, size);
        }
    );
    
    LOG_INFO("[ClientManager] WS Handlers registered.");
}

void ClientManager::handle_client_logon(CMClientPtr new_client, const AdminRequest* admin_req, uint64_t msg_seq_num)
{
    uint32_t client_id = admin_req->client_id();
    uint64_t client_msg_seq_num = msg_seq_num;
    uint64_t client_ack_seq_num = admin_req->ack_seq_num();

    uint64_t expected_msg_seq_num = 0;
    uint64_t expected_ack_seq_num = 0;

    CMClientPtr old_client;

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        old_client = it->second;
    }

    if (old_client) {
        expected_msg_seq_num = old_client->inbound_seq_num() + 1;
        expected_ack_seq_num = old_client->outbound_seq_num();
    } else {
        expected_msg_seq_num = db_->getClientISeqNum(client_id) + 1;
        expected_ack_seq_num = db_->getClientOSeqNum(client_id);
    }

    if (client_msg_seq_num != expected_msg_seq_num || client_ack_seq_num > expected_ack_seq_num) {
        LOG_INFO("[ClientManager] Client %d connection rejected. Expected msg: %lu, ack: %lu. Got msg: %lu, ack: %lu", client_id, expected_msg_seq_num, expected_ack_seq_num, client_msg_seq_num, client_ack_seq_num);
        
        flatbuffers::FlatBufferBuilder fbb(128);
        uint64_t rej_seq_num = old_client ? old_client->increment_outbound_seq_num() : db_->incrementAndGetClientOSeqNum(client_id);
        auto admin_resp = CreateAdminResponse(fbb, AdminResponseType_Reject, client_id, expected_msg_seq_num, RejectCode_InvalidSequenceNumber);
        auto client_resp = CreateClientResponse(fbb, ClientResponseData_AdminResponse, admin_resp.Union(), rej_seq_num);
        fbb.Finish(client_resp);
        new_client->send(fbb.GetBufferPointer(), fbb.GetSize());
        // Do not store session or mark ready
        return;
    }


    uint64_t shared_msg_seq_num = 0;

    if (old_client) {
        shared_msg_seq_num = old_client->increment_outbound_seq_num();
        Server::WSClientPtr old_ws = old_client->get_conn();
        if (old_ws && old_ws != new_client->get_conn()) {
            flatbuffers::FlatBufferBuilder fbb(128);
            auto admin_resp = CreateAdminResponse(fbb, AdminResponseType_Reject, client_id, expected_msg_seq_num, RejectCode_LoginAtOtherSession);
            auto client_resp = CreateClientResponse(fbb, ClientResponseData_AdminResponse, admin_resp.Union(), shared_msg_seq_num);
            fbb.Finish(client_resp);
            old_client->send(fbb.GetBufferPointer(), fbb.GetSize());
            
            old_ws->close();
        }
    } else {
        shared_msg_seq_num = expected_ack_seq_num + 1;
    }
    
    new_client->set_client_id(client_id);
    new_client->set_inbound_seq_num(client_msg_seq_num);
    new_client->set_outbound_seq_num(shared_msg_seq_num);
    clients_[client_id] = new_client;

    LOG_INFO("[ClientManager] Client %d connected.", client_id);
    
    // Send missed executions (OrderResponse)
    auto missed_responses = db_->getResponsesSince(client_id, client_ack_seq_num);
    LOG_INFO("[ClientManager] Sending %ld missed responses.", missed_responses.size());
    uint64_t resend_seq = client_ack_seq_num;
    for (auto& resp : missed_responses) {
        resend_seq++;
        flatbuffers::FlatBufferBuilder fbb;
        auto resp_offset = OrderResponse::Pack(fbb, &resp);
        auto client_resp = CreateClientResponse(fbb, ClientResponseData_OrderResponse, resp_offset.Union(), resend_seq);
        fbb.Finish(client_resp);
        new_client->send(fbb.GetBufferPointer(), fbb.GetSize());
        logOrderResponse(&resp, "[ClientManager] Resending Missed:");
    }

    // Set ready for this client session
    new_client->set_ready(true);

    // Send AdminResponse(Ready)
    flatbuffers::FlatBufferBuilder fbb(128);
    auto admin_resp = CreateAdminResponse(fbb, AdminResponseType_Ready, client_id, expected_msg_seq_num, RejectCode_None);
    auto client_resp = CreateClientResponse(fbb, ClientResponseData_AdminResponse, admin_resp.Union(), shared_msg_seq_num);
    fbb.Finish(client_resp);
    new_client->send(fbb.GetBufferPointer(), fbb.GetSize());
    
    LOG_INFO("[ClientManager] Client %d session ready.", client_id);
}

void ClientManager::handle_client_logout(CMClientPtr client)
{
    if (!client) return;
    uint32_t client_id = client->client_id();
    if (client_id == 0) return; // Unauthenticated client, nothing to remove from DB or map

    auto it = clients_.find(client_id);
    if (it != clients_.end() && it->second == client) {
        db_->setClientISeqNum(client_id, client->inbound_seq_num());
        db_->setClientOSeqNum(client_id, client->outbound_seq_num());
        clients_.erase(it);
    }
    
    if (last_popped_client_ == client) {
        last_popped_client_ = nullptr;
    }

    LOG_INFO("[ClientManager] Client %d disconnected.", client_id);
}

void ClientManager::process_client_request(CMClientPtr client, const void* data, size_t size)
{
    DTRACE_PROBE(exchange, req_entry);
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(data), size);
    if (!verifier.VerifyBuffer<ClientRequest>(nullptr)) {
        LOG_WARN("[ClientManager] Received invalid data from a client (failed flatbuffer verification).");
        return;
    }

    auto request = flatbuffers::GetRoot<ClientRequest>(data);
    auto type = request->data_type();
    uint64_t client_msg_seq_num = request->msg_seq_num();

    // Logon/Logoff can be executed without is_ready check
    if (type == ClientRequestData_AdminRequest) {
        auto admin_req = request->data_as_AdminRequest();
        if (admin_req->action() == AdminAction_LogOn) {
            handle_client_logon(client, admin_req, client_msg_seq_num);
        } else if (admin_req->action() == AdminAction_LogOut) {
            this->handle_client_logout(client);
        }
        return;
    }

    Server::WSClientPtr ws = client->get_conn();
    if (!client->is_ready()) {
        LOG_WARN("[ClientManager] Received non-admin request before logon completed.");
        return;
    }
    uint64_t expected_i_seq = client->inbound_seq_num() + 1;
    if (client_msg_seq_num != expected_i_seq) {
        LOG_WARN("[ClientManager] Seqnum mismatch. Expected %lu, got %lu", expected_i_seq, client_msg_seq_num);
        flatbuffers::FlatBufferBuilder fbb(128);
        uint64_t new_o_seq = client->increment_outbound_seq_num();
        auto admin_resp = CreateAdminResponse(fbb, AdminResponseType_Reject, client->client_id(), expected_i_seq, RejectCode_InvalidSequenceNumber);
        auto client_resp = CreateClientResponse(fbb, ClientResponseData_AdminResponse, admin_resp.Union(), new_o_seq);
        fbb.Finish(client_resp);
        client->send(fbb.GetBufferPointer(), fbb.GetSize());
        return;
    }
    client->increment_inbound_seq_num();

    switch (type) {
        case ClientRequestData_OrderRequest: {
            auto order_req = request->data_as_OrderRequest();
            DbSymbolInfo info;
            int32_t core_offset = 0; // default
            if (sym_db_->getSymbolInfo(order_req->symbol_id(), info)) { // TODO: impl a fast sym -> core 
                core_offset = info.core_offset;
            }

            if (core_offset < 0 || core_offset >= static_cast<int32_t>(request_rings_.size()) || !request_rings_[core_offset]) {
                LOG_WARN("[ClientManager] No request ring for core_offset %d", core_offset);
                return;
            }

            auto token = request_rings_[core_offset]->reserve(sizeof(OrderRequestT));
            if (!token) {
                // TODO: Send some alert
                return;
            }
            pending_order_clients_[core_offset].push(client);
            new (token->payload) OrderRequestT {
                .action      = order_req->action(),
                .exec_id     = order_req->exec_id(),
                .order_id    = order_req->order_id(),
                .client_id   = order_req->client_id(),
                .symbol_id   = order_req->symbol_id(),
                .side        = order_req->side(),
                .type        = order_req->type(),
                .p           = order_req->p(),
                .q           = order_req->q(),
                .visible_qty = order_req->visible_qty(),
                .timestamp   = order_req->timestamp(),
            };
            request_rings_[core_offset]->commit(*token);
            DTRACE_PROBE1(exchange, req_enqueue, order_req->exec_id());
            break;
        }
        case ClientRequestData_PositionRequest: {
            auto post_req = request->data_as_PositionRequest();
            int64_t pos = db_->getPosition(post_req->client_id(), post_req->symbol_id());
            
            PositionResponseT pos_resp_t;
            pos_resp_t.client_id = post_req->client_id();
            pos_resp_t.symbol_id = post_req->symbol_id();
            pos_resp_t.position = pos;
            client->send(&pos_resp_t);
            break;
        }
        case ClientRequestData_OpenOrderRequest: {
            auto open_req = request->data_as_OpenOrderRequest();
            uint32_t client_id = open_req->client_id();

            auto open_orders = db_->getOpenOrders(client_id);
            LOG_INFO("[ClientManager] Sending %zu open orders on request to client %d.", open_orders.size(), client->client_id());
            for (auto& order_resp_t : open_orders) {
                client->send(&order_resp_t);
            }
            break;
        }
        default: break;
    }
}

void ClientManager::handle_execution_response(const OrderResponseT* resp)
{
    DTRACE_PROBE1(exchange, exec_resp_entry, resp->exec_id);
    uint32_t client_id = resp->client_id;

    CMClientPtr client;
    if (check_exec(resp->exec_type, EXEC_RESP)) {
        DbSymbolInfo info;
        int32_t core_offset = 0; // default
        if (sym_db_->getSymbolInfo(resp->symbol_id, info)) { // TODO: impl a fast sym -> core 
            core_offset = info.core_offset;
        }

        if (core_offset < 0 || core_offset >= static_cast<int32_t>(pending_order_clients_.size())) {
            LOG_ERROR("[ClientManager] Queue bounds mismatch for core_offset %d", core_offset);
        } else {
            // TODO: Compare with/without queueing client
            auto& q = pending_order_clients_[core_offset]; // TODO: impl a fast core -> queue  
            if (!q.empty()) {
                client = q.front();
                q.pop();
                if (client->client_id() != client_id) {
                    LOG_ERROR("[ClientManager] Queue mismatch! expected %u, got %u", client_id, client->client_id());
                    client = nullptr; 
                } else {
                    last_popped_client_ = client;
                }
            } else {
                // LOG_ERROR("[ClientManager] Queue empty but got queue response for client %u", client_id);
            }
        }
    } else {
        if (last_popped_client_ && last_popped_client_->client_id() == client_id) {
            client = last_popped_client_;
        }
    }

    if (!client) {
        auto it = clients_.find(client_id);
        if (it != clients_.end()) {
            client = it->second;
        }
    }

    if (client) {
        client->send(resp);
        DTRACE_PROBE1(exchange, exec_resp_before_db, resp->exec_id);
    }    
}

int ClientManager::poll_client()
{
    return ws_adaptor_->poll();
}

int ClientManager::poll_server()
{
    const void* data = nullptr;
    uint32_t len = 0;
    if (!response_ring_->read_next(data, len)) {
        return 0;
    }

    if (len >= sizeof(OrderResponseT)) {
        auto resp = reinterpret_cast<const OrderResponseT*>(data);
        handle_execution_response(resp);
    }
    return 1;
}

} // namespace Exchange
