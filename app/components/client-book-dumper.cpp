#include "SimpleWSClient.hpp"
#include "L2Book.hpp"
#include "fbs/exchange_generated.h"
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

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <symbol_id> <output_csv>\n";
        return 1;
    }

    g_book.symbol_id = static_cast<uint32_t>(std::stoul(argv[1]));
    g_filepath = argv[2];

    std::signal(SIGINT, sigint_handler);

    try {
        auto md_client = SimpleWSClient::create("127.0.0.1", "9002");
        if (!md_client->connect()) {
            std::cerr << "Failed to connect to Market Data port 9002\n";
            return 1;
        }

        md_client->run_async([](const void* data, size_t size) {
            (void)size;
            auto md_update = flatbuffers::GetRoot<MarketDataUpdate>(data);
            if (md_update->data_type() == MarketDataUpdateData_L2Update) {
                auto update = md_update->data_as_L2Update();
                if (update->symbol_id() == g_book.symbol_id) {
                    g_book.update(update->side(), update->p(), update->q());
                }
            }
        });

        // Send subscribe request
        flatbuffers::FlatBufferBuilder fbb(128);
        auto req = CreateMarketDataRequest(fbb, g_book.symbol_id, MDType_L2, SubType_subscribe);
        fbb.Finish(req);
        md_client->send(fbb.GetBufferPointer(), fbb.GetSize());

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

        md_client->stop();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
