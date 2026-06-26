#include <iostream>
#include <memory>
#include <string>
#include "ring/SHMRingBuffer.hpp"
#include "fbs/exchange_generated.h"
#include "LogUtil.hpp"
#include "define.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"
#include "HttpServer.hpp"

using namespace Exchange;

int main() {
    Exchange::initLogger("HttpAccepter");
    LOG_INFO("================================================================================");

    try {
        boost::asio::io_context ioc{1};
        int main_core = OH_CORE;
        if (main_core >= 0) {
            set_thread_affinity(main_core, "HttpAccepter");
        }
        
        SHMRingBuffer request_ring(ORDER_REQUEST, ORDER_REQUEST_SIZE);
        
        auto handler = [&request_ring](const http::request<http::vector_body<char>>& req) -> http::response<http::string_body> {
            auto const version = req.version();
            
            if (req.method() == http::verb::post && req.target() == "/order" && req.body().size() >= 8) {
                auto order_req = flatbuffers::GetRoot<OrderRequest>(req.body().data());
                uint64_t exec_id = order_req->exec_id();
                
                logOrderRequest(order_req, "[Accepter] Received Request:");

                if (request_ring.enqueue(const_cast<char*>(req.body().data()), req.body().size())) {
                    LOG_INFO("[Accepter] Enqueued Request exec_id=%d size=%d", exec_id, req.body().size());
                    http::response<http::string_body> res{http::status::ok, version};
                    res.set(http::field::content_type, "text/plain");
                    res.body() = "Order received: exec_id=" + std::to_string(exec_id);
                    return res;
                } else {
                    LOG_ERROR("[Accepter] Failed to enqueue request for exec_id=%d", exec_id);
                    http::response<http::string_body> res{http::status::internal_server_error, version};
                    res.body() = "Internal Server Error: Queue Full";
                    return res;
                }
            }

            http::response<http::string_body> res{http::status::not_found, version};
            res.body() = "Not Found";
            return res;
        };

        HttpServer server(
            {boost::asio::ip::make_address("0.0.0.0"), PORT_OE},
            "POST, OPTIONS",
            handler
        );
        server.run(ioc);

        LOG_INFO("[Accepter] Listening on 0.0.0.0:%d (Coroutine mode via HttpServer)", PORT_OE);
        ioc.run();
    } catch (const std::exception& e) {
        LOG_ERROR("[Accepter] Main error: %d", e.what());
    }
    return 0;
}
