#pragma once

#include <cstddef>
#include <cstdint>

#include "fbs/order_generated.h"

namespace Exchange {

struct Order;

class ExecutionReporter
{
public:
    virtual ~ExecutionReporter() = default;

    virtual void onRequest(const OrderRequest* req) = 0;
    virtual void onAck(const OrderRequest* req, size_t price_index) = 0;
    virtual void onCancelled(const OrderRequest* req) = 0;
    virtual void onModified(const OrderRequest* req) = 0;
    virtual void onReject(const OrderRequest* req, RejectCode code) = 0;
    virtual void onFill(const Order* incoming,
                        const Order* existing,
                        int64_t price,
                        uint64_t qty_fill) = 0;
};

class StdoutExecutionReporter final : public ExecutionReporter
{
public:
    void onRequest(const OrderRequest* req) override;
    void onAck(const OrderRequest* req, size_t price_index) override;
    void onCancelled(const OrderRequest* req) override;
    void onModified(const OrderRequest* req) override;
    void onReject(const OrderRequest* req, RejectCode code) override;
    void onFill(const Order* incoming,
                const Order* existing,
                int64_t price,
                uint64_t qty_fill) override;
};

class ClientExecutionReporter final : public ExecutionReporter
{
public:
    void onRequest(const OrderRequest* req) override;
    void onAck(const OrderRequest* req, size_t price_index) override;
    void onCancelled(const OrderRequest* req) override;
    void onModified(const OrderRequest* req) override;
    void onReject(const OrderRequest* req, RejectCode code) override;
    void onFill(const Order* incoming,
                const Order* existing,
                int64_t price,
                uint64_t qty_fill) override;
};

} // namespace Exchange
