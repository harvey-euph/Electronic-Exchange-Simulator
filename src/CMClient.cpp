#include "CMClient.hpp"
#include "fbs/exchange_generated.h"

namespace Exchange {

CMClient::CMClient(WSClientPtr ws, uint32_t client_id) 
    : conn_(ws), client_id_(client_id) {}

void CMClient::set_message_handler(MessageHandler handler) { 
    msg_handler_ = handler; 
}

void CMClient::on_message(const void* data, size_t size) {
    if (msg_handler_) {
        msg_handler_(this->shared_from_this(), data, size);
    }
}

void CMClient::send(const void* data, size_t size) {
    if (conn_) {
        conn_->send(data, size);
    }
}

void CMClient::send(const OrderResponseT* resp) {
    if (!conn_) return;
    
    flatbuffers::FlatBufferBuilder fbb(256);
    auto resp_offset = OrderResponse::Pack(fbb, resp);
    auto client_resp = CreateClientResponse(fbb, ClientResponseData_OrderResponse, resp_offset.Union(), increment_outbound_seq_num());
    fbb.Finish(client_resp);
    
    send(fbb.GetBufferPointer(), fbb.GetSize());
}

void CMClient::send(const PositionResponseT* resp) {
    if (!conn_) return;
    
    flatbuffers::FlatBufferBuilder fbb(128);
    auto resp_offset = PositionResponse::Pack(fbb, resp);
    auto client_resp = CreateClientResponse(fbb, ClientResponseData_PositionResponse, resp_offset.Union(), increment_outbound_seq_num());
    fbb.Finish(client_resp);
    
    send(fbb.GetBufferPointer(), fbb.GetSize());
}

uint32_t CMClient::client_id() const { return client_id_; }
void CMClient::set_client_id(uint32_t id) { client_id_ = id; }

uint64_t CMClient::inbound_seq_num() const { return inbound_seq_num_.load(std::memory_order_relaxed); }
void CMClient::set_inbound_seq_num(uint64_t seq) { inbound_seq_num_.store(seq, std::memory_order_relaxed); }
uint64_t CMClient::increment_inbound_seq_num() { return inbound_seq_num_.fetch_add(1, std::memory_order_relaxed) + 1; }

uint64_t CMClient::outbound_seq_num() const { return outbound_seq_num_.load(std::memory_order_relaxed); }
void CMClient::set_outbound_seq_num(uint64_t seq) { outbound_seq_num_.store(seq, std::memory_order_relaxed); }
uint64_t CMClient::increment_outbound_seq_num() { return outbound_seq_num_.fetch_add(1, std::memory_order_relaxed) + 1; }

WSClientPtr CMClient::get_conn() const { return conn_; }

bool CMClient::is_ready() const { return ready_; }
void CMClient::set_ready(bool ready) { ready_ = ready; }

void CMClient::bind_adaptor(std::shared_ptr<WSAdaptor> adaptor, 
                            std::function<void(CMClientPtr)> on_open,
                            std::function<void(CMClientPtr)> on_close,
                            std::function<void(CMClientPtr, const void*, size_t)> on_message) 
{
    adaptor->set_open_handler([on_open, on_close, on_message](WSClientPtr ws) {
        auto client = std::make_shared<CMClient>(ws);

        if (on_message) {
            client->set_message_handler(on_message);
            ws->set_message_handler([client](const void* data, size_t size) {
                client->on_message(data, size);
            });
        }

        if (on_close) {
            ws->set_close_handler([client, on_close]() {
                on_close(client);
            });
        }

        if (on_open) {
            on_open(client);
        }
    });
}

} // namespace Exchange
