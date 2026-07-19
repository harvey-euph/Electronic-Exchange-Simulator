#include "service/OrderBook.hpp"
#include "define.hpp"
#include "util/LogUtil.hpp"
#include "util/TimeUtil.hpp"
#include "service/Snapshot.hpp"
#include <algorithm>
#include <random>
#include <fstream>
#include <sys/sdt.h>

using namespace Exchange;

OrderBook::OrderBook(
    uint64_t symbol_id,
    int64_t min_step, 
    int64_t price_index_offset, 
    size_t max_price_levels,
    mmaplog::MmapWriter* response_ring
)   : symbol_id_(symbol_id)
    , min_step_(min_step)
    , price_index_offset_(price_index_offset)
    , max_price_levels_(max_price_levels)
    , response_ring_(response_ring)
    , price_array_(max_price_levels_)
{
    resp.symbol_id = symbol_id_;

    if (min_step <= 0) {
        throw std::invalid_argument("min_step must be positive");
    }
    if (max_price_levels == 0) {
        throw std::invalid_argument("max_price_levels must be > 0");
    }
}

OrderBook::~OrderBook() {}

static Order* createOrder(const OrderRequestT* req)
{
    return new Order {
        (static_cast<uint64_t>(req->client_id) << 32) | req->order_id,
        req->q, req->q, req->type,
        nullptr, nullptr, nullptr,
        req->timestamp, req->symbol_id
    };
}

void OrderBook::handleNewOrder(const OrderRequestT* req, bool report_ack)
{
    uint64_t combined_order_id = (static_cast<uint64_t>(req->client_id) << 32) | req->order_id;

    DTRACE_PROBE1(exchange, ob_map_find_start, "active_orders");
    bool contains = active_orders_.contains(combined_order_id);
    DTRACE_PROBE(exchange, ob_map_find_end);
    if (contains) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_DuplicateOrderID);
        return;
    }

    if (req->p <= 0 && req->type == OrderType_Limit) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_PriceInvalid);
        return;
    }
    constexpr uint64_t GLOBAL_MAX_QTY = 1'000'000'000ULL;
    if (req->q <= 0 || req->q > GLOBAL_MAX_QTY) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_InvalidQuantity);
        return;
    }

    DTRACE_PROBE(matching_engine, ob_create_order_start);
    Order* taker = createOrder(req);
    DTRACE_PROBE(matching_engine, ob_create_order_end);

    if (report_ack) {
        sendResponse(ExecType_New, combined_order_id, req->exec_id, req->side, req->p, req->q, req->q);
    }

    const size_t price_idx = (req->type == OrderType_Market) ? 
        (req->side == Side_Buy ? max_price_levels_ - 1 : 0) : price_to_index(req->p);
        
    match(taker, req->side, price_idx);
}

void OrderBook::match(Order* taker, Side taker_side, size_t price_idx)
{
    DTRACE_PROBE(exchange, ob_match_start);
    const int side_int = static_cast<int>(taker_side);
    PriceLevel **oppo = &best_levels_[1^side_int];

    while (*oppo && taker->qty_remaining)
    {
        DTRACE_PROBE(exchange, ob_match_outer_start);
        const size_t oppo_idx = (*oppo) - price_array_.data();
        const size_t p = pl_to_price(*oppo);
        const bool crossed = (taker_side == Side_Buy) ? (price_idx >= oppo_idx) : (price_idx <= oppo_idx);
        if (!crossed) {
            DTRACE_PROBE(exchange, ob_match_outer_end);
            break;
        }

        Order* maker = (*oppo)->dummy_head.next;
        while (maker != &(*oppo)->dummy_tail && taker->qty_remaining)
        {
            DTRACE_PROBE(exchange, ob_match_inner_start);
            const uint64_t qty_fill = std::min(maker->qty_remaining, taker->qty_remaining);

            maker->qty_remaining -= qty_fill;
            taker->qty_remaining -= qty_fill;
            (*oppo)->total_qty   -= qty_fill;

            {
                static thread_local std::mt19937_64 gen(std::random_device{}());
                uint64_t exec_id = gen();
                const Side maker_side = static_cast<Side>(1^side_int);
                sendResponse(
                    taker->qty_remaining == 0 ? ExecType_Fill : ExecType_PartialFill,
                    taker->order_id, exec_id, taker_side, p, qty_fill, taker->qty_remaining);
                sendResponse(
                    maker->qty_remaining == 0 ? ExecType_Fill : ExecType_PartialFill,
                    maker->order_id, exec_id, maker_side, p, qty_fill, maker->qty_remaining);
            }

            if (maker->qty_remaining) {
                DTRACE_PROBE(exchange, ob_match_inner_end);
                continue;
            }

            DTRACE_PROBE1(exchange, ob_map_erase_start, "active_orders");
            active_orders_.erase(maker->order_id);
            DTRACE_PROBE(exchange, ob_map_erase_end);
            removeOrderFromLevel(maker);
            Order *next = maker->next;
            delete maker;
            maker = next;
            DTRACE_PROBE(exchange, ob_match_inner_end);
        }
        DTRACE_PROBE(exchange, ob_match_outer_end);

        if (!(*oppo)->order_count)
        {
            removePriceLevel(*oppo, (Side)(1^side_int));
            *oppo = best_levels_[1^side_int];
        }
    }

    if (!taker->qty_remaining)
    {
        DTRACE_PROBE1(exchange, ob_map_erase_start, "active_orders");
        active_orders_.erase(taker->order_id);
        DTRACE_PROBE(exchange, ob_map_erase_end);
        delete taker;
        DTRACE_PROBE(exchange, ob_match_end);
        return;
    }

    PriceLevel *pl = GetOrCreatePriceLevel(price_idx, taker_side);
    insertOrderToLevel(pl, taker, taker_side);

    DTRACE_PROBE1(exchange, ob_map_insert_start, "active_orders");
    active_orders_[taker->order_id] = taker;
    DTRACE_PROBE(exchange, ob_map_insert_end);
    DTRACE_PROBE(exchange, ob_match_end);
}

void OrderBook::handleCancelOrder(const OrderRequestT* req, bool report_cancelled)
{
    uint64_t combined_order_id = (static_cast<uint64_t>(req->client_id) << 32) | req->order_id;
    DTRACE_PROBE1(exchange, ob_map_find_start, "active_orders");
    auto it = active_orders_.find(combined_order_id);
    DTRACE_PROBE(exchange, ob_map_find_end);
    if (it == active_orders_.end()) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_OrderNotFound);
        return;
    }

    Order *o = it->second;

    DTRACE_PROBE1(exchange, ob_map_erase_start, "active_orders");
    active_orders_.erase(o->order_id);
    DTRACE_PROBE(exchange, ob_map_erase_end);
    removeOrderFromLevel(o);

    PriceLevel *pl = o->price_level;
    pl->total_qty -= o->qty_remaining;

    const size_t p = pl_to_price(pl);

    if (!pl->order_count)
        removePriceLevel(pl, req->side);

    if (report_cancelled) {
        sendResponse(ExecType_Cancelled, o->order_id, req->exec_id, req->side, p, o->qty_original, 0);
    }
    delete o;
}

void OrderBook::handleModifyOrder(const OrderRequestT* req)
{
    uint64_t combined_order_id = (static_cast<uint64_t>(req->client_id) << 32) | req->order_id;
    DTRACE_PROBE1(exchange, ob_map_find_start, "active_orders");
    auto it = active_orders_.find(combined_order_id);
    DTRACE_PROBE(exchange, ob_map_find_end);
    if (it == active_orders_.end()) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_OrderNotFound);
        return;
    }

    Order *o = it->second;
    
    constexpr uint64_t GLOBAL_MAX_QTY = 1'000'000'000ULL;
    if (req->q > GLOBAL_MAX_QTY) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, o->qty_remaining, RejectCode_InvalidQuantity);
        return;
    }
    
    if (req->p && price_invalid(req->p)) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, o->qty_remaining, RejectCode_PriceInvalid);
        return;
    }

    PriceLevel *pl = o->price_level;
    PriceLevel *target = req->p ? &price_array_[price_to_index(req->p)] : pl;
    const uint64_t new_qty = req->q ? req->q : o->qty_original;
    const uint64_t executed_qty = o->qty_original - o->qty_remaining;

    if (new_qty < executed_qty) {
        sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, o->qty_remaining, RejectCode_InvalidModify);
        return;
    }

    int64_t qty_diff = static_cast<int64_t>(new_qty) - static_cast<int64_t>(o->qty_original);
    
    const uint64_t new_qty_remaining = new_qty - executed_qty;

    // Check if nothing changes
    if (pl == target && qty_diff == 0) {
        sendResponse(ExecType_Replaced, combined_order_id, req->exec_id, req->side, pl_to_price(target), new_qty, new_qty_remaining);
        return;
    }

    bool needs_requeue = (pl != target || qty_diff > 0);

    // Send Replaced response first
    sendResponse(ExecType_Replaced, combined_order_id, req->exec_id, req->side, pl_to_price(target), new_qty, new_qty_remaining);

    if (!new_qty_remaining || needs_requeue) {
        removeOrderFromLevel(o);
        pl->total_qty -= o->qty_remaining;
        if (!pl->order_count) {
            removePriceLevel(pl, req->side);
        }
    } else {
        // In-place update (qty_diff < 0)
        pl->total_qty += qty_diff;
    }

    // If it fills immediately after replaced
    if (!new_qty_remaining) {
        sendResponse(ExecType_Fill, combined_order_id, req->exec_id, req->side, pl_to_price(target), 0, 0);
        DTRACE_PROBE1(exchange, ob_map_erase_start, "active_orders");
        active_orders_.erase(combined_order_id);
        DTRACE_PROBE(exchange, ob_map_erase_end);
        delete o;
        return;
    }

    o->qty_remaining = new_qty_remaining;
    o->qty_original = new_qty;

    if (needs_requeue) {
        const size_t price_idx = target - price_array_.data();
        match(o, req->side, price_idx);
    }
}

void OrderBook::processRequest(const OrderRequestT* req)
{
    if (!req) return;

    DTRACE_PROBE2(exchange, ob_req_entry, req->exec_id, req->action);

    switch (req->action) {
    case OrderAction_Cancel:
        DTRACE_PROBE(exchange, ob_cancel_start);
        handleCancelOrder(req);
        DTRACE_PROBE(exchange, ob_cancel_end);
        break;
    case OrderAction_Modify:
        DTRACE_PROBE(exchange, ob_modify_start);
        handleModifyOrder(req);
        DTRACE_PROBE(exchange, ob_modify_end);
        break;
    case OrderAction_New:
        DTRACE_PROBE(exchange, ob_new_start);
        if (req->type != OrderType_Market && price_invalid(req->p)) {
            uint64_t combined_order_id = (static_cast<uint64_t>(req->client_id) << 32) | req->order_id;
            sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_PriceInvalid);
        } else {
            handleNewOrder(req);
        }
        DTRACE_PROBE(exchange, ob_new_end);
        break;
    default:
        {
            uint64_t combined_order_id = (static_cast<uint64_t>(req->client_id) << 32) | req->order_id;
            sendResponse(ExecType_Rejected, combined_order_id, req->exec_id, req->side, req->p, req->q, 0, RejectCode_InvalidAction);
        }
        break;
    }

    DTRACE_PROBE2(exchange, ob_req_exit, req->exec_id, req->action);
}



void OrderBook::insertOrderToLevel(PriceLevel* pl, Order* order, [[maybe_unused]] Side side)
{
    order->price_level = pl;

    Order* old_tail = pl->dummy_tail.prev;

    old_tail->next = order;
    order->prev = old_tail;

    order->next = &pl->dummy_tail;
    pl->dummy_tail.prev = order;

    ++pl->order_count;
    pl->total_qty += order->qty_remaining;
}

void OrderBook::removeOrderFromLevel(Order *o)
{
    o->prev->next = o->next;
    o->next->prev = o->prev;
    o->price_level->order_count -= 1;
}

void OrderBook::removePriceLevel(PriceLevel *pl, Side side)
{
    if (!pl) return;

    const size_t price_idx = pl - price_array_.data();
    const int s = static_cast<int>(side);

    if (pl->better) {
        pl->better->worse = pl->worse;
    } else {
        best_levels_[s] = pl->worse;
    }

    if (pl->worse) {
        pl->worse->better = pl->better;
    }

    DTRACE_PROBE1(exchange, ob_map_erase_start, "active_levels");
    active_levels_[s].erase(price_idx);
    DTRACE_PROBE(exchange, ob_map_erase_end);
}

PriceLevel* OrderBook::GetOrCreatePriceLevel(size_t price_idx, Side side)
{
    PriceLevel* level = &price_array_[price_idx];

    if (level->order_count > 0) return level;

    level->better = nullptr;
    level->worse  = nullptr;

    const int s = static_cast<int>(side);

    auto& active = active_levels_[s];

    DTRACE_PROBE1(exchange, ob_map_find_start, "active_levels");
    auto it = active.lower_bound(price_idx);
    DTRACE_PROBE(exchange, ob_map_find_end);

    PriceLevel* better = nullptr;
    PriceLevel* worse  = nullptr;

    if (side == Side_Buy)
    {
        if (it != active.end())
        {
            better = it->second;
        }

        if (it != active.begin())
        {
            auto prev = it;
            --prev;
            worse = prev->second;
        }
    }
    else
    {
        if (it != active.begin())
        {
            auto prev = it;
            --prev;
            better = prev->second;
        }

        if (it != active.end())
        {
            worse = it->second;
        }
    }

    level->better = better;
    level->worse  = worse;

    if (better)
    {
        better->worse = level;
    }
    else
    {
        best_levels_[s] = level;
    }

    if (worse)
    {
        worse->better = level;
    }

    DTRACE_PROBE1(exchange, ob_map_insert_start, "active_levels");
    active[price_idx] = level;
    DTRACE_PROBE(exchange, ob_map_insert_end);

    return level;
}

void OrderBook::sendResponse(ExecType exec_type, uint64_t combined_order_id,
                             uint64_t exec_id, Side side, int64_t p, uint64_t q, uint64_t q_rem,
                             RejectCode reject_code)
{
    if (recover_mode_ || !response_ring_) return;
    
    uint64_t offset;
    DTRACE_PROBE1(exchange, ob_resp_reserve_start, exec_id);
    void* ptr = response_ring_->reserve(sizeof(OrderResponseT), offset);
    if (!ptr) return;

    DTRACE_PROBE1(exchange, ob_resp_new_start, exec_id);
    new (ptr) OrderResponseT {
        .exec_type = exec_type,
        .order_id = static_cast<uint32_t>(combined_order_id & 0xFFFFFFFF),
        .client_id = static_cast<uint32_t>(combined_order_id >> 32),
        .exec_id = exec_id,
        .symbol_id = this->symbol_id_,
        .side = side,
        .p = p,
        .q = q,
        .q_rem = q_rem,
        .reject_code = reject_code
    };
    
    DTRACE_PROBE1(exchange, ob_resp_commit_start, exec_id);
    response_ring_->commit(ptr);
    DTRACE_PROBE1(exchange, ob_resp_enqueue, exec_id);
}

void OrderBook::take_snapshot(const std::string& filepath) const
{
    std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc);
    if (!ofs) return;

    for (int side_idx = 0; side_idx < 2; ++side_idx) {
        Side side = static_cast<Side>(side_idx);
        for (const auto& [price_idx, pl] : active_levels_[side_idx]) {
            int64_t price = pl_to_price(pl);
            Order* o = pl->dummy_head.next;
            while (o != &pl->dummy_tail) {
                OrderSnapshot rec;
                rec.combined_order_id = o->order_id;
                rec.qty_original = o->qty_original;
                rec.qty_remaining = o->qty_remaining;
                rec.type = o->type;
                rec.side = side;
                rec.timestamp = o->timestamp;
                rec.symbol_id = o->symbol_id;
                rec.p = price;
                
                ofs.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
                o = o->next;
            }
        }
    }
}

void OrderBook::load_snapshot(const std::string& filepath)
{
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) return;

    OrderSnapshot rec;
    while (ifs.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
        Order* o = new Order{
            rec.combined_order_id,
            rec.qty_original,
            rec.qty_remaining,
            rec.type,
            nullptr, nullptr, nullptr,
            rec.timestamp,
            rec.symbol_id
        };

        const size_t price_idx = (rec.type == OrderType_Market) ? 
            (rec.side == Side_Buy ? max_price_levels_ - 1 : 0) : price_to_index(rec.p);

        PriceLevel *pl = GetOrCreatePriceLevel(price_idx, rec.side);
        insertOrderToLevel(pl, o, rec.side);
        active_orders_[o->order_id] = o;
    }
}

void OrderBook::restore_from_response(const OrderResponseT* resp)
{
    OrderRequestT req;
    req.client_id = resp->client_id;
    req.order_id = resp->order_id;
    req.exec_id = resp->exec_id;
    req.symbol_id = resp->symbol_id;
    req.side = resp->side;
    req.p = resp->p;
    req.q = resp->q;
    req.timestamp = 0; // Snapshot/journal replay does not strict need the old request timestamp

    if (resp->exec_type == ExecType_New) {
        req.action = OrderAction_New;
        req.type = (resp->p == 0) ? OrderType_Market : OrderType_Limit;
    } 
    else if (resp->exec_type == ExecType_Cancelled) {
        req.action = OrderAction_Cancel;
    } 
    else if (resp->exec_type == ExecType_Replaced) {
        req.action = OrderAction_Modify;
        uint64_t combined_order_id = (static_cast<uint64_t>(req.client_id) << 32) | req.order_id;
        auto it = active_orders_.find(combined_order_id);
        if (it != active_orders_.end()) {
            req.q = resp->q;
        }
    } 
    else {
        // Ignore ExecType_Fill, ExecType_PartialFill, ExecType_Rejected, etc.
        // The determinism of processRequest() will recreate these automatically.
        return; 
    }

    recover_mode_ = true;
    processRequest(&req);
    recover_mode_ = false;
}

void Exchange::OrderBook::dump_raw(const char* filepath) const {
    std::ofstream ofs(filepath);
    // Asks (side 1) from highest to lowest
    for (auto it = active_levels_[Side_Sell].rbegin(); it != active_levels_[Side_Sell].rend(); ++it) {
        ofs << "A," << pl_to_price(it->second) << "," << it->second->total_qty << "\n";
    }
    // Bids (side 0) from highest to lowest
    for (auto it = active_levels_[Side_Buy].rbegin(); it != active_levels_[Side_Buy].rend(); ++it) {
        ofs << "B," << pl_to_price(it->second) << "," << it->second->total_qty << "\n";
    }
}
