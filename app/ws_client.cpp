#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>

namespace beast = boost::beast;         
namespace websocket = beast::websocket; 
namespace net = boost::asio;            
using tcp = boost::asio::ip::tcp;       

int main(int argc, char** argv)
{
    try {
        std::string host = "127.0.0.1";
        std::string port = "9002";
        if (argc > 1) host = argv[1];
        if (argc > 2) port = argv[2];

        net::io_context ioc;
        tcp::resolver resolver{ioc};
        websocket::stream<beast::tcp_stream> ws{ioc};

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(ws).connect(results);

        ws.handshake(host, "/");
        std::cout << "[WS Client] Connected to " << host << ":" << port << std::endl;

        // Subscribe to symbol 1
        ws.write(net::buffer(std::string("sub 1")));
        std::cout << "[WS Client] Subscribed to symbol 1" << std::endl;

        for (;;) {
            beast::flat_buffer buffer;
            ws.read(buffer);
            std::cout << "[WS Client] Received message size: " << buffer.size() << std::endl;
        }
    }
    catch (std::exception const& e) {
        std::cerr << "[WS Client] Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
