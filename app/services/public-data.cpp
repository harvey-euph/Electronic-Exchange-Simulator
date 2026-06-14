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
#include "DbUtil.hpp"
#include "SymbolDatabase.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

net::awaitable<void> do_session(tcp::socket socket, std::shared_ptr<Exchange::SymbolDatabase> db)
{
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
                        std::string name;
                        int32_t price_exp = 0;
                        int64_t min_step = 0;
                        int64_t min_price = 0;
                        int64_t max_price = 0;
                        bool found = false;

                        Exchange::DbSymbolInfo info;
                        if (db && db->getSymbolInfo(symbol_id, info)) {
                            name = info.name;
                            price_exp = info.price_exp;
                            min_step = info.min_step;
                            min_price = info.min_price;
                            max_price = info.max_price;
                            found = true;
                        }


                        if (found) {
                            flatbuffers::FlatBufferBuilder builder(256);
                            auto name_offset = builder.CreateString(name);
                            Exchange::SymbolInfoBuilder symbol_builder(builder);
                            symbol_builder.add_symbol_id(symbol_id);
                            symbol_builder.add_name(name_offset);
                            symbol_builder.add_price_exp(price_exp);
                            symbol_builder.add_price_min_step(min_step);
                            symbol_builder.add_price_min(min_price);
                            symbol_builder.add_price_max(max_price);
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

net::awaitable<void> do_listen(tcp::endpoint endpoint, std::shared_ptr<Exchange::SymbolDatabase> db) {
    auto executor = co_await net::this_coro::executor;
    tcp::acceptor acceptor(executor, endpoint);

    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(executor, do_session(std::move(socket), db), net::detached);
    }
}

int main() {
    try {
        net::io_context ioc{1};
        int main_core = PUBLIC_DATA_MAIN_CORE;
        if (main_core >= 0) {
            Exchange::set_thread_affinity(main_core, "PublicData_Main");
        }
        
#ifdef USE_PGSQL
        auto db = std::make_shared<Exchange::PostgresSymbolDatabase>(Exchange::DbUtil::getConnectionString());
#else
        auto db = std::make_shared<Exchange::InMemorySymbolDatabase>();
#endif

        net::co_spawn(ioc, do_listen({net::ip::make_address("0.0.0.0"), PORT_PUBLIC_DATA}, db), net::detached);


        std::cout << "[PublicData] Listening on 0.0.0.0:" << PORT_PUBLIC_DATA << " (Coroutine mode)" << std::endl;
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "[PublicData] Main error: " << e.what() << std::endl;
    }
    return 0;
}
