#pragma once
#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <atomic>

namespace Exchange {

class WSClient {
public:
    virtual ~WSClient() = default;
    virtual void send(const void* data, size_t size) = 0;
    std::atomic_bool is_ready{false};
};

using WSClientPtr = std::shared_ptr<WSClient>;

/**
 * @brief WebSocket 適配器實作
 */
class WSAdaptor {
public:
    WSAdaptor(int port);
    virtual ~WSAdaptor();

    size_t poll();

    using MessageHandler = std::function<void(WSClientPtr client, const void* data, size_t size)>;
    void set_message_handler(MessageHandler handler);

    using CloseHandler = std::function<void(WSClientPtr client)>;
    void set_close_handler(CloseHandler handler);

    // Direct Sending (if app logic needs it)
    void send(WSClientPtr client, const void* data, size_t size);
    
    // Broadcast to all (ignoring symbol_id filters)
    void broadcast(const void* data, size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace Exchange
