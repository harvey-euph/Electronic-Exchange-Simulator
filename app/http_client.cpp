#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "csv_util.hpp"
#include "fbs/order_generated.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

int main(int argc, char** argv) {
    try {
        std::string csv_path = "data/basic.csv";
        if (argc > 1) {
            csv_path = argv[1];
        }

        std::string host = "127.0.0.1";
        std::string port = "8080";

        Exchange::CSVDataReader reader;
        if (!reader.loadFromCSV(csv_path)) {
            std::cerr << "Failed to load CSV: " << csv_path << std::endl;
            return EXIT_FAILURE;
        }

        const auto& requests = reader.getRequests();
        std::cout << "Loaded " << requests.size() << " orders from " << csv_path << std::endl;

        net::io_context ioc;
        tcp::resolver resolver{ioc};
        beast::tcp_stream stream{ioc};

        auto const results = resolver.resolve(host, port);
        stream.connect(results);

        for (const auto* order : requests) {
            flatbuffers::FlatBufferBuilder fbb;
            auto action = order->action();
            auto exec_id = order->exec_id();
            auto order_id = order->order_id();
            auto client_id = order->client_id();
            auto symbol_id = order->symbol_id();
            auto side = order->side();
            auto type = order->type();
            auto price = order->p();
            auto quantity = order->q();
            auto timestamp = order->timestamp();

            auto or_offset = Exchange::CreateOrderRequest(
                fbb, action, exec_id, order_id, client_id, symbol_id, side, type, price, quantity, 0, timestamp
            );
            fbb.Finish(or_offset);

            http::request<http::vector_body<char>> req{http::verb::post, "/order", 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/octet-stream");
            
            auto const* data = reinterpret_cast<const char*>(fbb.GetBufferPointer());
            req.body().assign(data, data + fbb.GetSize());
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            std::cout << "[HTTP Client] Sent exec_id=" << exec_id << " Response: " << res.body() << std::endl;

            // 1 second stop for each order
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (std::exception const& e) {
        std::cerr << "[HTTP Client] Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
