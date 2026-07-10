#pragma once

#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <string>
#include <functional>

namespace Exchange {

namespace http = boost::beast::http;
using tcp = boost::asio::ip::tcp;

using HttpRequestHandler = std::function<http::response<http::string_body>(
    const http::request<http::vector_body<char>>& req
)>;

class HttpServer {
public:
    HttpServer(tcp::endpoint endpoint, std::string allowed_methods, HttpRequestHandler handler);

    // Spawns the listener on the io_context
    void run(boost::asio::io_context& ioc);

private:
    boost::asio::awaitable<void> do_listen();
    boost::asio::awaitable<void> do_session(tcp::socket socket);

    tcp::endpoint endpoint_;
    std::string allowed_methods_;
    HttpRequestHandler handler_;
};

} // namespace Exchange
