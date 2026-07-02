#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Exchange {
struct AlgoTradingConfig {
    std::string host = "127.0.0.1";
    std::string mgmt_port = "9001";
    std::string l2_port = "9002";
    std::string l3_port = "9003";
    std::string http_port = "8080";
    bool use_http = false;
    uint32_t client_id = 101;
    std::vector<uint32_t> symbol_ids = {1};
    uint32_t timer_interval_ms = 1000;
};
}
