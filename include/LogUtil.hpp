#pragma once
#include <iostream>
#include "fbs/exchange_generated.h"
#include "Order.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/printf.h>

namespace Exchange {

inline void initLogger(const std::string& logger_name) {
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log/services.log", false);
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

#define LOG_INFO(fmt_str, ...) do { spdlog::info(fmt::sprintf(fmt_str, ##__VA_ARGS__)); } while(0)
#define LOG_WARN(fmt_str, ...) do { spdlog::warn(fmt::sprintf(fmt_str, ##__VA_ARGS__)); } while(0)
#define LOG_ERROR(fmt_str, ...) do { spdlog::error(fmt::sprintf(fmt_str, ##__VA_ARGS__)); } while(0)

inline void logOrderRequest(const OrderRequest* req, const char* prefix = "[OrderRequest]") {
    if (!req) return;
    LOG_INFO("%s action=%s, exec_id=%d, order_id=%d, client=%d, sym=%d, side=%s, type=%s, price=%d, qty=%d, visible=%d, ts=%d", prefix, EnumNameOrderAction(req->action()), req->exec_id(), req->order_id(), req->client_id(), req->symbol_id(), EnumNameSide(req->side()), EnumNameOrderType(req->type()), req->p(), req->q(), req->visible_qty(), req->timestamp());
}

inline void logOrderResponse(const OrderResponse* resp, const char* prefix = "[OrderResponse]") {
    if (!resp) return;
    LOG_INFO("%s exec_type=%s, order_id=%d, client=%d, exec_id=%d, symbol=%d, side=%s, p=%d, q=%d, reject=%s", prefix, EnumNameExecType(resp->exec_type()), resp->order_id(), resp->client_id(), resp->exec_id(), resp->symbol_id(), EnumNameSide(resp->side()), resp->p(), resp->q(), EnumNameRejectCode(resp->reject_code()));
}

inline void logOrderResponse(const OrderResponseT* resp, const char* prefix = "[OrderResponse]") {
    if (!resp) return;
    LOG_INFO("%s exec_type=%s, order_id=%d, client=%d, exec_id=%d, symbol=%d, side=%s, p=%d, q=%d, reject=%s", prefix, EnumNameExecType(resp->exec_type), resp->order_id, resp->client_id, resp->exec_id, resp->symbol_id, EnumNameSide(resp->side), resp->p, resp->q, EnumNameRejectCode(resp->reject_code));
}

inline void logOrder(const Order* o, const char* prefix = "[Order]") {
    if (!o) return;
    LOG_INFO("%s id=%d, client=%d, exec_id=%d, type=%s, qty_orig=%d, qty_rem=%d, ts=%d%d", prefix, o->order_id, o->client_id, o->exec_id, EnumNameOrderType(o->type), o->qty_original, o->qty_remaining, o->timestamp, (o->price_level ? " [InBook]" : " [Floating]"));
}

inline void logPositionResponse(const PositionResponse* resp, const char* prefix = "[PositionResponse]") {
    if (!resp) return;
    LOG_INFO("%s client=%d, symbol=%d, position=%d", prefix, resp->client_id(), resp->symbol_id(), resp->position());
}

inline void logL2Update(const L2Update* update, const char* prefix = "[L2Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        LOG_INFO("%s Snapshot Start | Symbol: %d", prefix, update->symbol_id());
        return;
    }
    LOG_INFO("%s symbol=%d, side=%s, price=%d, qty=%d, seq=%d, ts=%d", prefix, update->symbol_id(), EnumNameSide(update->side()), update->p(), update->q(), update->seq_num(), update->timestamp());
}

inline void logL3Update(const L3Update* update, const char* prefix = "[L3Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        LOG_INFO("%s Snapshot Start | Symbol: %d", prefix, update->symbol_id());
        return;
    }
    LOG_INFO("%s symbol=%d, type=%s, order_id=%d, side=%s, price=%d, qty=%d, seq=%d, ts=%d", prefix, update->symbol_id(), EnumNameExecType(update->exec_type()), update->order_id(), EnumNameSide(update->side()), update->p(), update->q(), update->seq_num(), update->timestamp());
}

} // namespace Exchange
