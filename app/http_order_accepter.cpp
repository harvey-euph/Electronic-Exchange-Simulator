#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include "ring/SHMRingBuffer.hpp"
#include "fbs/order_generated.h"

namespace beast = boost::beast;         
namespace http = beast::http;           
namespace net = boost::asio;            
using tcp = boost::asio::ip::tcp;       

class session : public std::enable_shared_from_this<session> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    Exchange::SHMRingBuffer& request_ring_;
public:
    session(tcp::socket&& socket, Exchange::SHMRingBuffer& ring) : stream_(std::move(socket)), request_ring_(ring) {}
    void run() { do_read(); }
    void do_read() {
        auto req = std::make_shared<http::request<http::vector_body<char>>>();
        http::async_read(stream_, buffer_, *req, beast::bind_front_handler(&session::on_read, shared_from_this(), req));
    }
    void on_read(std::shared_ptr<http::request<http::vector_body<char>>> req, beast::error_code ec, std::size_t) {
        if (ec) return;
        handle_request(std::move(*req));
    }
    void handle_request(http::request<http::vector_body<char>>&& req)
    {
        auto const version = req.version();
        auto const set_cors = [](auto& res) {
            res.set(http::field::access_control_allow_origin, "*");
            res.set(http::field::access_control_allow_methods, "POST, OPTIONS");
            res.set(http::field::access_control_allow_headers, "Content-Type");
        };
        if (req.method() == http::verb::options) {
            auto res = std::make_shared<http::response<http::empty_body>>(http::status::ok, version);
            set_cors(*res); res->prepare_payload();
            http::async_write(stream_, *res, [this, self=shared_from_this(), res](beast::error_code ec, std::size_t){ if(!ec) do_read(); });
            return;
        }
        if (req.method() == http::verb::post && req.target() == "/order" && req.body().size() >= 8) {
            auto order_req = flatbuffers::GetRoot<Exchange::OrderRequest>(req.body().data());
            uint64_t exec_id = order_req->exec_id();
            std::cout << "[Accepter] Received Request exec_id=" << exec_id << " order_id=" << order_req->order_id() << std::endl;
            
            if (request_ring_.enqueue(req.body().data(), req.body().size())) {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, version);
                res->set(http::field::content_type, "text/plain");
                set_cors(*res); 
                res->body() = "Order received: exec_id=" + std::to_string(exec_id);
                res->prepare_payload();
                std::cout << "[Accepter] Async response sent for exec_id=" << exec_id << std::endl;
                http::async_write(stream_, *res, [this, self=shared_from_this(), res](beast::error_code ec, std::size_t){ if(!ec) do_read(); });
                return;
            } else {
                std::cerr << "[Accepter] Failed to enqueue request for exec_id=" << exec_id << std::endl;
                auto res = std::make_shared<http::response<http::string_body>>(http::status::internal_server_error, version);
                set_cors(*res); res->body() = "Internal Server Error: Queue Full"; res->prepare_payload();
                http::async_write(stream_, *res, [this, self=shared_from_this(), res](beast::error_code ec, std::size_t){ if(!ec) do_read(); });
                return;
            }
        }
        auto res = std::make_shared<http::response<http::string_body>>(http::status::not_found, version);
        set_cors(*res); res->body() = "Not Found"; res->prepare_payload();
        http::async_write(stream_, *res, [this, self=shared_from_this(), res](beast::error_code ec, std::size_t){ if(!ec) do_read(); });
    }
};

int main()
{
    try {
        net::io_context ioc{1};
        Exchange::SHMRingBuffer request_ring("OrderRequest", 16384);
        tcp::acceptor acceptor{ioc, {net::ip::make_address("0.0.0.0"), 8080}};
        std::cout << "[Accepter] Listening on 0.0.0.0:8080 (Async mode)" << std::endl;
        std::function<void()> do_accept = [&](){
            acceptor.async_accept([&](beast::error_code ec, tcp::socket socket){
                if(!ec) std::make_shared<session>(std::move(socket), request_ring)->run();
                do_accept();
            });
        };
        do_accept(); ioc.run();
    } catch (const std::exception& e) { std::cerr << "[Accepter] Main error: " << e.what() << std::endl; }
    return 0;
}
