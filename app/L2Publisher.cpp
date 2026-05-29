#include "ring/SHMRingBuffer.hpp"
#include "fbs/order_generated.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <memory>

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

namespace Exchange {

/**
 * @brief L2 輸出適配器抽象基底類別 (Interface)
 * 直接接收 FlatBuffers 的強型別結構體指針，達成零拷貝傳遞
 */
class L2OutputAdaptor {
public:
    virtual ~L2OutputAdaptor() = default;
    
    // 直接接受 FlatBuffers 生成的結構體指標
    virtual void publish(const Exchange::L2Update* l2_update) = 0;
};

/**
 * @brief 標準輸出適配器實作 (將原本的列印邏輯抽離至此)
 */
class StdoutAdaptor : public L2OutputAdaptor {
public:
    void publish(const Exchange::L2Update* l2_update) override 
    {
        uint32_t symbol_id  = l2_update->symbol_id();
        uint64_t seq        = l2_update->seq_num();
        Exchange::Side side = l2_update->side();
        int64_t price       = l2_update->p();
        uint64_t qty        = l2_update->q();
        uint64_t ts         = l2_update->timestamp();

        std::string side_str = (side == Exchange::Side_Buy) ? "BUY" : "SELL";

        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::system_clock::now().time_since_epoch()
                   ).count();
        int64_t latency = now - static_cast<int64_t>(ts);

        std::cout << "[L2] Seq: " << seq 
                  << " | Symbol: " << symbol_id 
                  << " | " << side_str 
                  << " | Price: " << price 
                  << " | Qty: " << qty 
                  << " | Latency: " << latency << " us\n";
    }
};

} // namespace Exchange

int main() 
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string ring_name = "L2_Update_Ring"; 
    size_t ring_size = 16384;

    std::cout << "[L2Publisher] Connecting to SHMRingBuffer: " << ring_name << "..." << std::endl;
    
    Exchange::SHMRingBuffer* ring_buffer = nullptr;
    try {
        ring_buffer = new Exchange::SHMRingBuffer(ring_name, ring_size);
    } catch (const std::exception& e) {
        std::cerr << "[L2Publisher] Failed to connect to SHMRingBuffer: " << e.what() << std::endl;
        return -1;
    }

    std::unique_ptr<Exchange::L2OutputAdaptor> adaptor = std::make_unique<Exchange::StdoutAdaptor>();

    std::cout << "[L2Publisher] Connected successfully. Start consuming with Adaptor..." << std::endl;

    void* data_ptr = nullptr;
    size_t data_size = 0;

    // 2. 進入高效能消費無窮迴圈
    while (g_running.load(std::memory_order_relaxed))
    {
        if (ring_buffer->dequeue(&data_ptr, &data_size))
        {
            if (data_ptr == nullptr || data_size == 0) {
                continue;
            }

            auto l2_update = flatbuffers::GetRoot<Exchange::L2Update>(data_ptr);
            adaptor->publish(l2_update);
        } 
        else 
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(500));
        }
    }

    std::cout << "[L2Publisher] Exiting and cleaning up..." << std::endl;
    delete ring_buffer;
    
    return 0;
}