#include "client/OrderEntryClient.hpp"
#include "util/csv_util.hpp"
#include "util/LogUtil.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstdlib>
#include <vector>

namespace Exchange {

class CSVSender : public OrderEntryClient {
public:
    CSVSender(const Config& config, const std::string& csv_path)
        : TradingClientBase(config), OrderEntryClient(config), csv_path_(csv_path) {
        if (!reader_.loadFromCSV(csv_path_)) {
            throw std::runtime_error("Failed to load CSV: " + csv_path_);
        }
        std::cout << "[CSVSender] Loaded " << reader_.getRequests().size() << " orders from " << csv_path_ << std::endl;
    }

    void on_order_response(const OrderResponse* response) override {
        logOrderResponse(response, "[CSVSender]");
        
        if (response->exec_id() == expected_exec_id_ || response->order_id() == expected_order_id_) {
            if (!next_sent_) {
                next_sent_ = true;
                send_next_order();
            }
        }
    }

    void on_position_response(const PositionResponse* response) override {
        logPositionResponse(response, "[CSVSender]");
    }

    void on_timer() override {}

    int run_sync() {
        fetch_symbols_info();
        if (start() != 0) return 1;
        
        wait_until_ready();
        std::cout << "[CSVSender] Server ready signal received." << std::endl;
        
        const auto& requests = reader_.getRequests();
        if (requests.empty()) {
            return 0;
        }
        
        current_idx_ = -1;
        next_sent_ = true;
        send_next_order();
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return 0;
    }

private:
    void send_next_order() {
        const auto& requests = reader_.getRequests();
        current_idx_++;
        if (current_idx_ < requests.size()) {
            OrderRequestT req;
            requests[current_idx_]->UnPackTo(&req);
            
            expected_exec_id_ = req.exec_id;
            expected_order_id_ = req.order_id;
            next_sent_ = false;
            
            // Bypass send_order_request to preserve client_id, order_id, exec_id, timestamp
            uint64_t msg_seq_num = ++o_seq_num_;
            flatbuffers::FlatBufferBuilder fbb(256);
            auto order_offset = OrderRequest::Pack(fbb, &req);
            auto client_req = CreateClientRequest(fbb, ClientRequestData_OrderRequest, order_offset.Union(), msg_seq_num);
            fbb.Finish(client_req);
            mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
            if (current_idx_ % 100 == 0 || current_idx_ == requests.size() - 1) {
                std::cout << "[CSVSender] Sent order (" << current_idx_ + 1 << "/" << requests.size() << "): action=" << EnumNameOrderAction(req.action)
                          << ", symbol=" << req.symbol_id << ", side=" << EnumNameSide(req.side)
                          << ", p=" << req.p << ", q=" << req.q << std::endl;
            }
        } else {
            std::cout << "[CSVSender] All orders sent and responded. Terminating..." << std::endl;
            running_ = false;
        }
    }

    std::string csv_path_;
    CSVDataReader reader_;
    int current_idx_ = -1;
    bool next_sent_ = false;
    uint64_t expected_exec_id_ = 0;
    uint64_t expected_order_id_ = 0;
};

} // namespace Exchange

int main(int argc, char** argv) {
    std::string csv_path = "data/basic.csv";
    if (argc > 1) {
        csv_path = argv[1];
    }

    Exchange::AlgoTradingConfig config;
    config.timer_interval_ms = 50;
    
    try {
        Exchange::CSVSender client(config, csv_path);
        return client.run_sync();
    } catch (const std::exception& e) {
        std::cerr << "[CSVSender] Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
