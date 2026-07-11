#pragma once
#include <string>
#include <functional>
#include <memory>

namespace Exchange {
namespace Client {

class WSClient {
public:
    using MessageHandler = std::function<void(const void* data, size_t size)>;

    static std::unique_ptr<WSClient> create(const std::string& host, const std::string& port);

    virtual ~WSClient() = default;

    virtual bool connect() = 0;
    virtual void run_async(MessageHandler on_message) = 0;
    
    virtual void send(const void* data, size_t size) = 0;
    virtual void send_text(const std::string& text) = 0;

    virtual void stop() = 0;
};

} // namespace Client
} // namespace Exchange
