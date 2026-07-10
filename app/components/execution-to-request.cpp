#include "ipc/mmap_log.h"
#include "define.hpp"
#include "fbs/exchange_generated.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace Exchange;

int main() {
    // 輸出 CSV 標題
    std::cout << "req_id,order_id,client_id,action,side,type,price,quantity,visible_qty,timestamp,symbol_id" << std::endl;
    mmaplog::MmapReader reader(EXECUTION_JOURNAL_DIR);

    uint64_t req_id = 1;
    uint64_t fake_timestamp = 1747238400000000000ULL;

    while (true) {
        const void* data = nullptr;
        uint32_t len = 0;
        if (reader.read_next(data, len)) {
            if (len >= sizeof(OrderResponseT)) {
                const OrderResponseT* resp = reinterpret_cast<const OrderResponseT*>(data);
                
                std::string action_str;
                switch (resp->exec_type) {
                    case ExecType_New:       action_str = "New"; break;
                    case ExecType_Replaced:  action_str = "Modify"; break;
                    case ExecType_Cancelled: action_str = "Cancel"; break;
                    default: continue; // 忽略其他 execution
                }

                std::string side_str = (resp->side == Side_Buy) ? "Buy" : (resp->side == Side_Sell) ? "Sell" : "Unknown";
                if (side_str == "Unknown") continue;

                int64_t price = resp->p;
                uint64_t quantity = resp->q;
                
                // 在 CSV 解析端，Cancel 的價格與數量通常為 0
                if (resp->exec_type == ExecType_Cancelled) {
                    price = 0;
                    quantity = 0;
                }

                std::cout << req_id++ << ","
                          << resp->order_id << ","
                          << resp->client_id << ","
                          << action_str << ","
                          << side_str << ","
                          << "Limit,"
                          << price << ","
                          << quantity << ","
                          << 0 << "," // visible_qty
                          << fake_timestamp++ << ","
                          << resp->symbol_id
                          << std::endl;
            } else {
                std::cerr << "[execution-to-request] Warning: Read invalid length " << len << std::endl;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
