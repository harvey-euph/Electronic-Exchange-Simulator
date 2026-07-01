#include "MDClient.hpp"

namespace Exchange {

MDClient::MDClient(WSClientPtr ws) : conn_(ws) {}

void MDClient::set_message_handler(MessageHandler handler) {
    msg_handler_ = handler;
}

void MDClient::on_message(const void* data, size_t size) {
    if (msg_handler_) {
        msg_handler_(this->shared_from_this(), data, size);
    }
}

void MDClient::send(const void* data, size_t size) {
    if (conn_) {
        conn_->send(data, size);
    }
}

void MDClient::bind_adaptor(std::shared_ptr<WSAdaptor> adaptor, 
                            std::function<void(MDClientPtr)> on_open,
                            std::function<void(MDClientPtr)> on_close,
                            std::function<void(MDClientPtr, const void*, size_t)> on_message) 
{
    adaptor->set_open_handler([on_open, on_close, on_message](WSClientPtr ws) {
        auto client = std::make_shared<MDClient>(ws);

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
