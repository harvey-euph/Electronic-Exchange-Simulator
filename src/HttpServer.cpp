#include "LogUtil.hpp"
#include "HttpServer.hpp"
#include <boost/beast/core.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>

namespace Exchange {

namespace beast = boost::beast;
namespace net = boost::asio;

HttpServer::HttpServer(tcp::endpoint endpoint, std::string allowed_methods, HttpRequestHandler handler)
    : endpoint_(endpoint), allowed_methods_(std::move(allowed_methods)), handler_(std::move(handler)) {}

void HttpServer::run(net::io_context& ioc) {
    net::co_spawn(ioc, [this]() { return do_listen(); }, net::detached);
}

net::awaitable<void> HttpServer::do_listen() {
    auto executor = co_await net::this_coro::executor;
    try {
        tcp::acceptor acceptor(executor, endpoint_);

        for (;;) {
            tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
            net::co_spawn(executor, [this, s = std::move(socket)]() mutable {
                return do_session(std::move(s));
            }, net::detached);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[HttpServer] Acceptor error: %s", e.what());
    }
}

net::awaitable<void> HttpServer::do_session(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;

    auto set_cors = [this](auto& res) {
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, allowed_methods_);
        res.set(http::field::access_control_allow_headers, "Content-Type");
    };

    try {
        for (;;) {
            http::request<http::vector_body<char>> req;
            co_await http::async_read(stream, buffer, req, net::use_awaitable);

            auto const version = req.version();

            if (req.method() == http::verb::options) {
                http::response<http::empty_body> res{http::status::ok, version};
                set_cors(res);
                res.prepare_payload();
                co_await http::async_write(stream, res, net::use_awaitable);
                continue;
            }

            http::response<http::string_body> res = handler_(req);
            set_cors(res);
            res.prepare_payload();
            co_await http::async_write(stream, res, net::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != beast::error::timeout && e.code() != http::error::end_of_stream) {
            LOG_ERROR("[HttpServer] Session error: %s", e.what());
        }
    }
}

} // namespace Exchange
