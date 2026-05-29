#include "OrderBook.hpp"
#include "ExecutionReporter.hpp"

#include <cstdio>
#include <iostream>

namespace Exchange {

namespace {
const char* action_name(const OrderRequest* req)
{
    return req ? EnumNameOrderAction(req->action()) : "";
}

const char* side_name(const OrderRequest* req)
{
    return req ? EnumNameSide(req->side()) : "";
}

void print_client_channel(const char* event, uint32_t client_id, uint64_t order_id)
{
    std::printf("[client] event=%s client_id=%u order_id=%lu\n",
                event, client_id, order_id);
}
} // namespace

void StdoutExecutionReporter::onRequest(const OrderRequest* req)
{
    if (!req) return;

    std::printf("[Order %lu] %s %s Price:%ld Qty:%lu Vis:%lu Client:%u\n",
                req->order_id(),
                side_name(req),
                action_name(req),
                req->p(),
                req->q(),
                req->visible_qty(),
                req->client_id());
}

void StdoutExecutionReporter::onAck(const OrderRequest* req, size_t price_index)
{
    if (!req) return;

    std::printf("[ACK] order_id=%lu client_id=%u type=%d price_idx=%zu qty=%lu ts=%lu\n",
                req->order_id(),
                req->client_id(),
                static_cast<int>(req->type()),
                price_index,
                req->q(),
                req->timestamp());
}

void StdoutExecutionReporter::onCancelled(const OrderRequest* req)
{
    if (!req) return;

    std::printf("[CANCELLED] order_id=%lu client_id=%u\n",
                req->order_id(),
                req->client_id());
}

void StdoutExecutionReporter::onModified(const OrderRequest* req)
{
    if (!req) return;

    std::printf("[MODIFIED] order_id=%lu client_id=%u price=%ld qty=%lu\n",
                req->order_id(),
                req->client_id(),
                req->p(),
                req->q());
}

void StdoutExecutionReporter::onReject(const OrderRequest* req, RejectCode code)
{
    if (!req) return;

    std::printf("[REJECT] client_id=%u code=%s(%d)\n",
                req->client_id(),
                EnumNameRejectCode(code),
                static_cast<int>(code));
}

void StdoutExecutionReporter::onFill(const Order* incoming,
                                     const Order* existing,
                                     int64_t price,
                                     uint64_t qty_fill)
{
    if (!incoming || !existing) return;

    std::printf("[FILL] taker_order=%lu maker_order=%lu price=%ld qty=%lu "
                "taker_remaining=%lu maker_remaining=%lu\n",
                incoming->order_id,
                existing->order_id,
                price,
                qty_fill,
                incoming->qty_remaining,
                existing->qty_remaining);
}

void ClientExecutionReporter::onRequest(const OrderRequest* req)
{
    if (!req) return;
    print_client_channel("request", req->client_id(), req->order_id());
}

void ClientExecutionReporter::onAck(const OrderRequest* req, size_t price_index)
{
    if (!req) return;
    std::printf("[client] event=ack client_id=%u order_id=%lu price_idx=%zu\n",
                req->client_id(), req->order_id(), price_index);
}

void ClientExecutionReporter::onCancelled(const OrderRequest* req)
{
    if (!req) return;
    print_client_channel("cancelled", req->client_id(), req->order_id());
}

void ClientExecutionReporter::onModified(const OrderRequest* req)
{
    if (!req) return;
    std::printf("[client] event=modified client_id=%u order_id=%lu price=%ld qty=%lu\n",
                req->client_id(), req->order_id(), req->p(), req->q());
}

void ClientExecutionReporter::onReject(const OrderRequest* req, RejectCode code)
{
    if (!req) return;
    std::printf("[client] event=reject client_id=%u order_id=%lu code=%s(%d)\n",
                req->client_id(),
                req->order_id(),
                EnumNameRejectCode(code),
                static_cast<int>(code));
}

void ClientExecutionReporter::onFill(const Order* incoming,
                                     const Order* existing,
                                     int64_t price,
                                     uint64_t qty_fill)
{
    if (!incoming || !existing) return;
    std::printf("[client] event=fill taker=%lu maker=%lu price=%ld qty=%lu\n",
                incoming->order_id, existing->order_id, price, qty_fill);
}

} // namespace Exchange
