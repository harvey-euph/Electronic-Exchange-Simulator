#include "client/MarketDataClient.hpp"
#include "client/L2Book.hpp"
#include <iostream>
#include <fstream>
#include <csignal>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

using namespace Exchange;

L2Book g_book;
std::string g_filepath;
std::atomic<bool> g_running{true};

void sigint_handler(int) {
    g_running = false;
}

class BookDumperClient : public MarketDataClient {
public:
    BookDumperClient(const Config& config) : TradingClientBase(config), MarketDataClient(config) {}

    void on_l2_update(uint32_t symbol_id, const L2Update* update) override {
        if (symbol_id == g_book.symbol_id) {
            g_book.update(update->side(), update->p(), update->q());
        }
    }

    void on_l3_update(uint32_t, const L3Update*) override {} // Ignore
    
    void init() {
        fetch_symbols_info();
    }
};

int main(int argc, char** argv) 
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <symbol_id> <output_csv>\n";
        return 1;
    }

    g_book.symbol_id = static_cast<uint32_t>(std::stoul(argv[1]));
    g_filepath = argv[2];

    std::signal(SIGINT, sigint_handler);

    try {
        AlgoTradingConfig config;
        config.symbol_ids = {g_book.symbol_id};

        BookDumperClient client(config);
        client.init();
        
        if (client.start() != 0) {
            std::cerr << "Failed to start MarketDataClient\n";
            return 1;
        }

        // Block until SIGINT
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::ofstream ofs(g_filepath);
        if (ofs.is_open()) {
            g_book.display_raw(ofs);
        } else {
            std::cerr << "Failed to open output file: " << g_filepath << std::endl;
        }

        client.stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
