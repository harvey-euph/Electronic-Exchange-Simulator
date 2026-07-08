#pragma once

#include "WSAdaptor.hpp"
#include <memory>
#include <functional>
#include <atomic>

namespace Exchange {

struct OrderResponseT;
struct PositionResponseT;

class CMClient;
using CMClientPtr = std::shared_ptr<CMClient>;

class CMClient : public std::enable_shared_from_this<CMClient> {
public:
    using MessageHandler = std::function<void(CMClientPtr, const void*, size_t)>;

    explicit CMClient(WSClientPtr ws, uint32_t client_id = 0);

    void set_message_handler(MessageHandler handler);
    void on_message(const void* data, size_t size);
    void send(const void* data, size_t size);
    void send(const OrderResponseT* resp);
    void send(const PositionResponseT* resp);

    uint32_t client_id() const;
    void set_client_id(uint32_t id);

    uint64_t inbound_seq_num() const;
    void set_inbound_seq_num(uint64_t seq);
    uint64_t increment_inbound_seq_num();

    uint64_t outbound_seq_num() const;
    void set_outbound_seq_num(uint64_t seq);
    uint64_t increment_outbound_seq_num();

    WSClientPtr get_conn() const;

    bool is_ready() const;
    void set_ready(bool ready);

    static void bind_adaptor(std::shared_ptr<WSAdaptor> adaptor, 
                             std::function<void(CMClientPtr)> on_open,
                             std::function<void(CMClientPtr)> on_close,
                             std::function<void(CMClientPtr, const void*, size_t)> on_message);

private:
    WSClientPtr conn_;
    MessageHandler msg_handler_;
    
    uint32_t client_id_{0};
    std::atomic<uint64_t> inbound_seq_num_{0};
    std::atomic<uint64_t> outbound_seq_num_{0};
    bool ready_{false};
};

} // namespace Exchange
