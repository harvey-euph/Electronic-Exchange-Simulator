#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "fbs/exchange_generated.h"
#include "LogUtil.hpp"
#include "define.hpp"
#include "ThreadUtil.hpp"
#include "AffinityConfig.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct SymbolData {
    uint32_t symbol_id;
    std::string name;
    int32_t price_exp;
    int64_t price_min_step;
    int64_t price_min;
    int64_t price_max;
};

// In-memory database of symbols
const std::unordered_map<uint32_t, SymbolData> symbol_db = {
    {1, {1, "BTC", -2, 25, 3000000, 12000000}}, // BTC-USD: step=0.25, min=30000.00, max=120000.00
    {2, {2, "ETH", -2, 10, 150000, 600000}},    // ETH-USD: step=0.10, min=1500.00, max=6000.00
    {3, {3, "SOL", -3, 5, 5000, 500000}}        // SOL-USD: step=0.005, min=5.000, max=500.000
};

net::awaitable<void> do_session(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;

    auto set_cors = [](auto& res) {
        res.set(http::field::access_control_allow_origin, "*");
        res.set(http::field::access_control_allow_methods, "GET, OPTIONS");
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

            if (req.method() == http::verb::get) {
                std::string target = std::string(req.target());
                std::string prefix = "/v1/symbol/";
                if (target.rfind(prefix, 0) == 0) {
                    std::string id_str = target.substr(prefix.length());
                    try {
                        uint32_t symbol_id = std::stoul(id_str);
                        auto it = symbol_db.find(symbol_id);
                        if (it != symbol_db.end()) {
                            const auto& sym = it->second;
                            flatbuffers::FlatBufferBuilder builder(256);
                            auto name_offset = builder.CreateString(sym.name);
                            Exchange::SymbolInfoBuilder symbol_builder(builder);
                            symbol_builder.add_symbol_id(sym.symbol_id);
                            symbol_builder.add_name(name_offset);
                            symbol_builder.add_price_exp(sym.price_exp);
                            symbol_builder.add_price_min_step(sym.price_min_step);
                            symbol_builder.add_price_min(sym.price_min);
                            symbol_builder.add_price_max(sym.price_max);
                            auto symbol_offset = symbol_builder.Finish();
                            builder.Finish(symbol_offset);

                            http::response<http::string_body> res{http::status::ok, version};
                            res.set(http::field::content_type, "application/octet-stream");
                            set_cors(res);
                            res.body() = std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
                            res.prepare_payload();
                            co_await http::async_write(stream, res, net::use_awaitable);
                            continue;
                        }
                    } catch (...) {}
                }
            }

            http::response<http::string_body> res{http::status::not_found, version};
            set_cors(res);
            res.body() = "Not Found";
            res.prepare_payload();
            co_await http::async_write(stream, res, net::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != beast::error::timeout && e.code() != http::error::end_of_stream) {
            std::cerr << "[PublicData] Session error: " << e.what() << std::endl;
        }
    }
}

net::awaitable<void> do_listen(tcp::endpoint endpoint) {
    auto executor = co_await net::this_coro::executor;
    tcp::acceptor acceptor(executor, endpoint);

    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(executor, do_session(std::move(socket)), net::detached);
    }
}

int main() {
    try {
        net::io_context ioc{1};
        int main_core = PUBLIC_DATA_MAIN_CORE;
        if (main_core >= 0) {
            Exchange::set_thread_affinity(main_core, "PublicData_Main");
        }
        
        net::co_spawn(ioc, do_listen({net::ip::make_address("0.0.0.0"), 8081}), net::detached);

        std::cout << "[PublicData] Listening on 0.0.0.0:8081 (Coroutine mode)" << std::endl;
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "[PublicData] Main error: " << e.what() << std::endl;
    }
    return 0;
}
