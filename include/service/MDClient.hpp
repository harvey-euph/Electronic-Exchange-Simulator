#pragma once

#include "service/WSAdaptor.hpp"
#include <memory>
#include <functional>

namespace Exchange {

class MDClient;
using MDClientPtr = std::shared_ptr<MDClient>;

class MDClient : public std::enable_shared_from_this<MDClient> {
public:
    using MessageHandler = std::function<void(MDClientPtr, const void*, size_t)>;

    explicit MDClient(Server::WSClientPtr ws) : ws_(ws) {}
    void set_message_handler(MessageHandler handler);
    void on_message(const void* data, size_t size);
    void send(const void* data, size_t size);

    static void bind_adaptor(std::shared_ptr<WSAdaptor> adaptor, 
                             std::function<void(MDClientPtr)> on_open,
                             std::function<void(MDClientPtr)> on_close,
                             std::function<void(MDClientPtr, const void*, size_t)> on_message);

private:
    Server::WSClientPtr ws_;
    MessageHandler msg_handler_;
};

} // namespace Exchange
