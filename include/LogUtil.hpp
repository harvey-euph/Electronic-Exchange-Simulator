#pragma once
#include <iostream>
#include "fbs/exchange_generated.h"
#include "Order.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Exchange {

inline void initLogger(const std::string& logger_name) {
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/exchange-all.log", false);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        
        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
        
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(logger);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    }
}

#define LOG_INFO(...) do { std::ostringstream _oss; _oss << __VA_ARGS__; std::string _s = _oss.str(); if(!_s.empty() && _s.back()=='\n') _s.pop_back(); spdlog::info(_s); } while(0)
#define LOG_WARN(...) do { std::ostringstream _oss; _oss << __VA_ARGS__; std::string _s = _oss.str(); if(!_s.empty() && _s.back()=='\n') _s.pop_back(); spdlog::warn(_s); } while(0)
#define LOG_ERROR(...) do { std::ostringstream _oss; _oss << __VA_ARGS__; std::string _s = _oss.str(); if(!_s.empty() && _s.back()=='\n') _s.pop_back(); spdlog::error(_s); } while(0)






inline void logOrderRequest(const OrderRequest* req, const char* prefix = "[OrderRequest]") {
    if (!req) return;
    LOG_INFO(prefix << " "
              << "action=" << EnumNameOrderAction(req->action())
              << ", exec_id=" << req->exec_id()
              << ", order_id=" << req->order_id()
              << ", client=" << req->client_id()
              << ", sym=" << req->symbol_id()
              << ", side=" << EnumNameSide(req->side())
              << ", type=" << EnumNameOrderType(req->type())
              << ", price=" << req->p()
              << ", qty=" << req->q()
              << ", visible=" << req->visible_qty()
              << ", ts=" << req->timestamp()
              );
}

inline void logOrderResponse(const OrderResponse* resp, const char* prefix = "[OrderResponse]") {
    if (!resp) return;
    LOG_INFO(prefix << " "
              << "exec_type=" << EnumNameExecType(resp->exec_type())
              << ", order_id=" << resp->order_id()
              << ", client=" << resp->client_id()
              << ", exec_id=" << resp->exec_id()
              << ", symbol=" << resp->symbol_id()
              << ", side=" << EnumNameSide(resp->side())
              << ", p=" << resp->p()
              << ", q=" << resp->q()
              << ", reject=" << EnumNameRejectCode(resp->reject_code())
              );
}

inline void logOrderResponse(const OrderResponseT* resp, const char* prefix = "[OrderResponse]") {
    if (!resp) return;
    LOG_INFO(prefix << " "
              << "exec_type=" << EnumNameExecType(resp->exec_type)
              << ", order_id=" << resp->order_id
              << ", client=" << resp->client_id
              << ", exec_id=" << resp->exec_id
              << ", symbol=" << resp->symbol_id
              << ", side=" << EnumNameSide(resp->side)
              << ", p=" << resp->p
              << ", q=" << resp->q
              << ", reject=" << EnumNameRejectCode(resp->reject_code)
              );
}

inline void logOrder(const Order* o, const char* prefix = "[Order]") {
    if (!o) return;
    LOG_INFO(prefix << " "
              << "id=" << o->order_id
              << ", client=" << o->client_id
              << ", exec_id=" << o->exec_id
              << ", type=" << EnumNameOrderType(o->type)
              << ", qty_orig=" << o->qty_original
              << ", qty_rem=" << o->qty_remaining
              << ", ts=" << o->timestamp
              << (o->price_level ? " [InBook]" : " [Floating]")
              );
}

inline void logPositionResponse(const PositionResponse* resp, const char* prefix = "[PositionResponse]") {
    if (!resp) return;
    LOG_INFO(prefix << " "
              << "client=" << resp->client_id()
              << ", symbol=" << resp->symbol_id()
              << ", position=" << resp->position()
              );
}

inline void logL2Update(const L2Update* update, const char* prefix = "[L2Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        LOG_INFO(prefix << " Snapshot Start | Symbol: " << update->symbol_id() );
        return;
    }
    LOG_INFO(prefix << " "
              << "symbol=" << update->symbol_id()
              << ", side=" << EnumNameSide(update->side())
              << ", price=" << update->p()
              << ", qty=" << update->q()
              << ", seq=" << update->seq_num()
              << ", ts=" << update->timestamp()
              );
}

inline void logL3Update(const L3Update* update, const char* prefix = "[L3Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        LOG_INFO(prefix << " Snapshot Start | Symbol: " << update->symbol_id() );
        return;
    }
    LOG_INFO(prefix << " "
              << "symbol=" << update->symbol_id()
              << ", type=" << EnumNameExecType(update->exec_type())
              << ", order_id=" << update->order_id()
              << ", side=" << EnumNameSide(update->side())
              << ", price=" << update->p()
              << ", qty=" << update->q()
              << ", seq=" << update->seq_num()
              << ", ts=" << update->timestamp()
              );
}

} // namespace Exchange
