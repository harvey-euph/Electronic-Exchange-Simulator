#pragma once
#include "fbs/order_generated.h"
#include <iostream>
#include <chrono>
#include <string>

namespace Exchange {

class L3OutputAdaptor {
public:
    virtual ~L3OutputAdaptor() = default;
    virtual void publish(const Exchange::L3Update* l3_update, const void* raw_data, size_t raw_size) = 0;
};

class StdoutL3Adaptor : public L3OutputAdaptor {
public:
    void publish(const Exchange::L3Update* l3_update, const void* /*raw_data*/, size_t /*raw_size*/) override 
    {
        uint32_t symbol_id  = l3_update->symbol_id();
        uint64_t seq        = l3_update->seq_num();
        Exchange::ExecType exec_type = l3_update->exec_type();
        uint64_t order_id   = l3_update->order_id();
        Exchange::Side side = l3_update->side();
        int64_t price       = l3_update->p();
        uint64_t qty        = l3_update->q();
        uint64_t ts         = l3_update->timestamp();

        std::string side_str = (side == Exchange::Side_Buy) ? "BUY" : (side == Exchange::Side_Sell ? "SELL" : "NONE");

        auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::system_clock::now().time_since_epoch()
                   ).count();
        int64_t latency = now - static_cast<int64_t>(ts);

        std::cout << "[L3] Seq: " << seq 
                  << " | Type: " << EnumNameExecType(exec_type)
                  << " | ID: " << order_id
                  << " | Symbol: " << symbol_id 
                  << " | " << side_str 
                  << " | Price: " << price 
                  << " | Qty: " << qty 
                  << " | Latency: " << latency << " us\n";
    }
};

} // namespace Exchange
