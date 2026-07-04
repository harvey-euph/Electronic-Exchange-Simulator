#include "OrderEntryClient.hpp"
#include "LogUtil.hpp"
#include <chrono>
#include <thread>

namespace Exchange {

OrderEntryClient::OrderEntryClient(const Config& config) : TradingClientBase(config) {
    mgmt_client_ = SimpleWSClient::create(config.host, config.mgmt_port);
}

OrderEntryClient::~OrderEntryClient() {
    stop_oe();
}

int OrderEntryClient::start_oe() {
    if (!mgmt_client_->connect()) {
        LOG_ERROR("Failed to connect to Management port %s", config_.mgmt_port.c_str());
        return 1;
    }

    mgmt_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto resp = flatbuffers::GetRoot<ClientResponse>(data);
        if (resp->data_type() == ClientResponseData_AdminResponse) {
            auto admin_resp = resp->data_as_AdminResponse();
            if (admin_resp->type() == AdminResponseType_Ready) {
                i_seq_num_ = admin_resp->msg_seq_num();
                // Request initial state explicitly
                request_open_orders();
                for (auto sym : config_.symbol_ids) {
                    request_position(sym);
                }
                
                {
                    std::lock_guard<std::mutex> lock(ready_mtx_);
                    ready_ = true;
                }
                ready_cv_.notify_all();
            } else {
                RejectCode reason = admin_resp->reject_code();
                LOG_ERROR("[OrderEntryClient] Login rejected: code=%s", EnumNameRejectCode(reason));
                if (reason == RejectCode_InvalidSequenceNumber) {
                    o_seq_num_ = admin_resp->expected_msg_seq_num();
                    // Send new LogOn with resynced seq numbers
                    flatbuffers::FlatBufferBuilder fbb(128);
                    auto username = fbb.CreateString("client_" + std::to_string(config_.client_id));
                    auto req = CreateAdminRequest(fbb, AdminAction_LogOn, config_.client_id, username, o_seq_num_, admin_resp->expected_ack_seq_num());
                    auto client_req = CreateClientRequest(fbb, ClientRequestData_AdminRequest, req.Union());
                    fbb.Finish(client_req);
                    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
                    LOG_INFO("[OrderEntryClient] Sent resynced LogOn request with MSG=%d, ACK=%d", o_seq_num_, admin_resp->expected_ack_seq_num());
                } else if (reason == RejectCode_LoginAtOtherSession) {
                    LOG_ERROR("[OrderEntryClient] Logged in at another session. Disconnecting.");
                    stop_oe();
                }
            }
            return;
        } else if (resp->data_type() == ClientResponseData_OrderResponse) {
            auto order_resp = resp->data_as_OrderResponse();
            if (order_resp->exec_type() != ExecType_OrderStatus) {
                if (order_resp->msg_seq_num() != i_seq_num_ + 1) {
                    LOG_ERROR("[OrderEntryClient] Sequence number mismatch. Expected %lu, got %lu", i_seq_num_ + 1, order_resp->msg_seq_num());
                    throw std::runtime_error("Sequence number mismatch on OrderResponse");
                }
                i_seq_num_ = order_resp->msg_seq_num();
            }
            on_order_response(order_resp);
        } else if (resp->data_type() == ClientResponseData_PositionResponse) {
            on_position_response(resp->data_as_PositionResponse());
        }
    });

    // Subscriptions
    {
        flatbuffers::FlatBufferBuilder fbb(128);
        auto username = fbb.CreateString("client_" + std::to_string(config_.client_id));
        ++o_seq_num_;
        auto admin_req = CreateAdminRequest(fbb, AdminAction_LogOn, config_.client_id, username, o_seq_num_, 0);
        auto client_req = CreateClientRequest(fbb, ClientRequestData_AdminRequest, admin_req.Union());
        fbb.Finish(client_req);
        mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
    }

    return 0;
}

int OrderEntryClient::run_oe() {
    fetch_symbols_info();
    if (start_oe() != 0) return 1;
    while (oe_running_) {
        on_timer();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.timer_interval_ms));
    }
    return 0;
}

void OrderEntryClient::stop_oe() {
    oe_running_ = false;
    if (mgmt_client_) mgmt_client_->stop();
}

void OrderEntryClient::new_limit_order(uint32_t symbol_id, Side side, int64_t p, uint64_t q, uint64_t visible_qty) {
    new_order(symbol_id, side, OrderType_Limit, p, q, visible_qty);
}

void OrderEntryClient::new_market_order(uint32_t symbol_id, Side side, uint64_t q) {
    new_order(symbol_id, side, OrderType_Market, 0, q, q);
}

void OrderEntryClient::new_order(uint32_t symbol_id, Side side, OrderType type, int64_t p, uint64_t q, uint64_t visible_qty) {
    OrderRequestT req;
    req.action = OrderAction_New;
    req.symbol_id = symbol_id;
    req.side = side;
    req.type = type;
    req.p = p;
    req.q = q;
    req.visible_qty = (visible_qty == 0) ? q : visible_qty;
    send_order_request(req);
}

void OrderEntryClient::replace_order(uint32_t order_id, int64_t p, uint64_t q, uint32_t symbol_id, Side side) {
    OrderRequestT req;
    req.action = OrderAction_Modify;
    req.order_id = order_id;
    req.symbol_id = symbol_id;
    req.side = side;
    req.p = p;
    req.q = q;
    send_order_request(req);
}

void OrderEntryClient::cancel_order(uint32_t order_id, uint32_t symbol_id, Side side) {
    OrderRequestT req;
    req.action = OrderAction_Cancel;
    req.order_id = order_id;
    req.symbol_id = symbol_id;
    req.side = side;
    send_order_request(req);
}

void OrderEntryClient::request_open_orders() {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto req = CreateOpenOrderRequest(fbb, config_.client_id);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_OpenOrderRequest, req.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

void OrderEntryClient::request_position(uint32_t symbol_id) {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto req = CreatePositionRequest(fbb, config_.client_id, symbol_id);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_PositionRequest, req.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

void OrderEntryClient::send_order_request(OrderRequestT& order) {
    if (order.action == OrderAction_New && order.type != OrderType_Market) {
        std::string err;
        if (!validate_price(order.symbol_id, order.p, err)) {
            LOG_ERROR("[OrderEntryClient] ERROR: Trying to send order with invalid price: %s", err.c_str());
            throw std::runtime_error("Invalid order price: " + err);
        }
    } else if (order.action == OrderAction_Modify) {
        std::string err;
        if (!validate_price(order.symbol_id, order.p, err)) {
            LOG_ERROR("[OrderEntryClient] ERROR: Trying to modify order with invalid price: %s", err.c_str());
            throw std::runtime_error("Invalid order price: " + err);
        }
    }

    order.client_id = config_.client_id;
    if (order.action == OrderAction_New) {
        order.order_id = next_id_++;
        order.exec_id = order.order_id;
    } else {
        order.exec_id = next_id_++;
    }

    order.msg_seq_num = ++o_seq_num_;

    order.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    flatbuffers::FlatBufferBuilder fbb(256);
    auto order_offset = OrderRequest::Pack(fbb, &order);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_OrderRequest, order_offset.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

void OrderEntryClient::query_position(uint32_t symbol_id) {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto pos_req = CreatePositionRequest(fbb, config_.client_id, symbol_id);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_PositionRequest, pos_req.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

void OrderEntryClient::on_order_response(const OrderResponse* response) {
    auto exec = response->exec_type();
    if ((exec == ExecType_New || exec == ExecType_Fill || 
         exec == ExecType_PartialFill || exec == ExecType_Replaced || exec == ExecType_OrderStatus) && 
        response->reject_code() == RejectCode_None) {
        std::string err;
        if (!validate_price(response->symbol_id(), response->p(), err)) {
            LOG_ERROR("[OrderEntryClient] ERROR: OrderResponse has invalid price: %s", err.c_str());
            throw std::runtime_error("OrderResponse has invalid price: " + err);
        }
    }
    account_.handle_order_response(response);
}

void OrderEntryClient::on_position_response(const PositionResponse* response) {
    account_.handle_position_response(response);
}

void OrderEntryClient::wait_until_ready() {
    std::unique_lock<std::mutex> lock(ready_mtx_);
    ready_cv_.wait(lock, [this] { return ready_.load(); });
}

}
