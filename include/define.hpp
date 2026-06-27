#pragma once
#include <thread>
#include <chrono>
#include "fbs/exchange_generated.h"

#define ORDER_REQUEST  "ORDER_REQUEST"
#define ORDER_RESPONSE "ORDER_RESPONSE"
#define ORDER_REQUEST_SIZE  65536
#define ORDER_RESPONSE_SIZE 131072
#define EXECUTION_JOURNAL_DIR "./log/execution-journals"

#define PORT_CM 9001
#define PORT_MD 9002
#define PORT_OE 8080
#define PORT_DT 8081

// Unified sleep duration in milliseconds for dev/test environment polling loops
#define POLL_SLEEP_MS 1

// Polling back-off strategy:
// In PRODUCTION_MODE, busy-wait using CPU pause instruction (or yield on non-x86) to minimize latency.
// In dev mode, sleep for POLL_SLEEP_MS to prevent CPU starvation.
#ifdef PRODUCTION_MODE
  #if defined(__x86_64__) || defined(_M_X64)
    #define POLL_BACKOFF() __builtin_ia32_pause()
  #else
    #define POLL_BACKOFF() std::this_thread::yield()
  #endif
#else
  #define POLL_BACKOFF() std::this_thread::sleep_for(std::chrono::milliseconds(POLL_SLEEP_MS))
#endif

namespace Exchange {

#define exec_mask(type) (1u << static_cast<unsigned>(type))
#define check_exec(type, mask) (exec_mask(type) & (mask))

constexpr uint32_t EXEC_NEW = exec_mask(ExecType_New);
constexpr uint32_t EXEC_PAR = exec_mask(ExecType_PartialFill);
constexpr uint32_t EXEC_FIL = exec_mask(ExecType_Fill);
constexpr uint32_t EXEC_REJ = exec_mask(ExecType_Rejected);
constexpr uint32_t EXEC_DEL = exec_mask(ExecType_Cancelled);
constexpr uint32_t EXEC_MOD = exec_mask(ExecType_Replaced);
constexpr uint32_t EXEC_GET = exec_mask(ExecType_OrderStatus);
constexpr uint32_t EXEC_END = exec_mask(ExecType_Complete);

constexpr uint32_t EXEC_NON   = EXEC_GET | EXEC_END ;
constexpr uint32_t EXEC_TRADE = EXEC_FIL | EXEC_PAR ;
constexpr uint32_t EXEC_PUT   = EXEC_MOD | EXEC_DEL ;
constexpr uint32_t EXEC_ANN   = EXEC_FIL | EXEC_DEL ;
constexpr uint32_t EXEC_ME    = EXEC_NEW | EXEC_MOD ;

constexpr uint32_t EXEC_ALIVE = EXEC_ME  | EXEC_PAR ;
constexpr uint32_t EXEC_RESP  = EXEC_ME  | EXEC_DEL | EXEC_REJ ;

constexpr uint32_t EXEC_MD    = EXEC_ANN | EXEC_ALIVE ;
constexpr uint32_t EXEC_EXEC  = EXEC_REJ | EXEC_MD;

} // namespace Exchange