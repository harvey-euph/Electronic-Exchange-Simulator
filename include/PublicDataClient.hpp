#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>
#include <stdexcept>
#include "fbs/exchange_generated.h"

namespace Exchange {

class PublicDataClient {
public:
    static std::unique_ptr<Exchange::SymbolInfoT> getSymbolInfo(
        const std::string& host,
        const std::string& port,
        uint32_t symbol_id) 
    {
        namespace beast = boost::beast;
        namespace http = beast::http;
        namespace net = boost::asio;
        using tcp = boost::asio::ip::tcp;

        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve(host, port);
        stream.connect(results);

        std::string target = "/v1/symbol/" + std::to_string(symbol_id);
        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::accept, "application/octet-stream");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::vector_body<char>> res;
        http::read(stream, buffer, res);

        if (res.result() != http::status::ok) {
            throw std::runtime_error("HTTP request failed with status: " + std::to_string(res.result_int()));
        }

        const auto& body = res.body();
        if (body.size() == 0) {
            throw std::runtime_error("Received empty response body");
        }

        // Verify FlatBuffer buffer
        flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(body.data()), body.size());
        if (!verifier.VerifyBuffer<Exchange::SymbolInfo>(nullptr)) {
            throw std::runtime_error("Invalid FlatBuffer SymbolInfo payload");
        }

        auto symbol_info = flatbuffers::GetRoot<Exchange::SymbolInfo>(body.data());
        auto unpacked = std::make_unique<Exchange::SymbolInfoT>();
        symbol_info->UnPackTo(unpacked.get());
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return unpacked;
    }
};

} // namespace Exchange
