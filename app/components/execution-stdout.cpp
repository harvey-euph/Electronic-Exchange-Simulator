#include "mmap_log.h"
#include "define.hpp"
#include "fbs/exchange_generated.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace Exchange;

int main() {
    std::cout << "[ExecutionStdout] Starting execution journal poller..." << std::endl;
    mmaplog::MmapReader reader(EXECUTION_JOURNAL_DIR);

    size_t max_p_len = 0;
    size_t max_q_len = 0;

    while (true) {
        const void* data = nullptr;
        uint32_t len = 0;
        if (reader.read_next(data, len)) {
            if (len >= sizeof(OrderResponseT)) {
                const OrderResponseT* resp = reinterpret_cast<const OrderResponseT*>(data);
                std::string exec_str;
                switch (resp->exec_type) {
                    case ExecType_New: exec_str = "NEW"; break;
                    case ExecType_Replaced: exec_str = "MOD"; break;
                    case ExecType_Cancelled: exec_str = "CAN"; break;
                    case ExecType_Fill: exec_str = "FIL"; break;
                    case ExecType_PartialFill: exec_str = "PAR"; break;
                    case ExecType_Rejected: exec_str = "REJ"; break;
                    default: exec_str = "OTH"; break;
                }

                std::string side_str = (resp->side == Side_Buy) ? "BID" : (resp->side == Side_Sell) ? "ASK" : "NON";

                std::string p_str = std::to_string(resp->p);
                std::string q_str = std::to_string(resp->q);
                
                if (p_str.length() > max_p_len) max_p_len = p_str.length();
                if (q_str.length() > max_q_len) max_q_len = q_str.length();

                // Build aligned strings manually by padding left with spaces
                std::string p_aligned = std::string(max_p_len - p_str.length(), ' ') + p_str;
                std::string q_aligned = std::string(max_q_len - q_str.length(), ' ') + q_str;

                std::cout << "[ExecutionStdout] [" << exec_str << "] [" << side_str << "] @ "
                          << p_aligned << " x " << q_aligned 
                          << " | order_id: " << resp->order_id
                        //   << ", exec_id: " << resp->exec_id
                          << ", client_id: " << resp->client_id
                          << ", symbol_id: " << resp->symbol_id
                          << ", msg_seq_num: " << resp->msg_seq_num 
                          << std::endl;
            } else {
                std::cout << "[ExecutionStdout] Warning: Read invalid length " << len << std::endl;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
