#!/usr/bin/env python3
import csv
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: python reference-matcher.py <requests.csv>")
        sys.exit(1)
        
    requests_file = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "."
    
    responses = []
    
    active_orders = {} # (client_id, order_id) -> dict
    positions = {} # client_id -> int
    
    # price levels: price -> list of (client_id, order_id)
    bids = {} 
    asks = {}
    
    def remove_order(client_id, order_id):
        combined = (client_id, order_id)
        if combined not in active_orders:
            return
        o = active_orders[combined]
        book = bids if o['side'] == 'Buy' else asks
        p = o['price']
        if p in book:
            if combined in book[p]:
                book[p].remove(combined)
            if not book[p]:
                del book[p]
        del active_orders[combined]
        
    def add_order(o):
        combined = (o['client_id'], o['order_id'])
        active_orders[combined] = o
        book = bids if o['side'] == 'Buy' else asks
        p = o['price']
        if p not in book:
            book[p] = []
        book[p].append(combined)

    def send_resp(exec_type, order_id, client_id, side, p, q, q_rem):
        responses.append({
            'exec_type': exec_type,
            'order_id': order_id,
            'client_id': client_id,
            'side': side,
            'p': p,
            'q': q,
            'q_rem': q_rem
        })

    def match(taker):
        is_buy = (taker['side'] == 'Buy')
        book = asks if is_buy else bids
        
        while taker['qty_remaining'] > 0:
            if not book:
                break
                
            best_p = min(book.keys()) if is_buy else max(book.keys())
            
            if is_buy and taker['price'] < best_p:
                break
            if not is_buy and taker['price'] > best_p:
                break
                
            maker_ids = book[best_p]
            if not maker_ids:
                del book[best_p]
                continue
                
            maker_combined = maker_ids[0]
            maker = active_orders[maker_combined]
            
            fill_qty = min(taker['qty_remaining'], maker['qty_remaining'])
            taker['qty_remaining'] -= fill_qty
            maker['qty_remaining'] -= fill_qty
            
            if taker['side'] == 'Buy':
                positions[taker['client_id']] = positions.get(taker['client_id'], 0) + fill_qty
                positions[maker['client_id']] = positions.get(maker['client_id'], 0) - fill_qty
            else:
                positions[taker['client_id']] = positions.get(taker['client_id'], 0) - fill_qty
                positions[maker['client_id']] = positions.get(maker['client_id'], 0) + fill_qty
            
            t_exec = 'FIL' if taker['qty_remaining'] == 0 else 'PAR'
            send_resp(t_exec, taker['order_id'], taker['client_id'], taker['side'], best_p, fill_qty, taker['qty_remaining'])
            
            m_exec = 'FIL' if maker['qty_remaining'] == 0 else 'PAR'
            send_resp(m_exec, maker['order_id'], maker['client_id'], maker['side'], best_p, fill_qty, maker['qty_remaining'])
            
            if maker['qty_remaining'] == 0:
                remove_order(maker['client_id'], maker['order_id'])
                
        return taker

    with open(requests_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row['req_id'].startswith('#'):
                continue
            order_id = int(row['order_id'])
            client_id = int(row['client_id'])
            action = row['action']
            side = row['side']
            price = int(row['price'])
            qty = int(row['quantity'])
            
            combined = (client_id, order_id)

            if action == 'New':
                if price <= 0:
                    send_resp('REJ', order_id, client_id, side, price, qty, 0)
                    continue
                if combined in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty, 0)
                    continue
                
                send_resp('NEW', order_id, client_id, side, price, qty, qty)
                
                taker = {
                    'order_id': order_id,
                    'client_id': client_id,
                    'side': side,
                    'price': price,
                    'qty_original': qty,
                    'qty_remaining': qty
                }
                
                taker = match(taker)
                if taker['qty_remaining'] > 0:
                    add_order(taker)

            elif action == 'Cancel':
                if combined not in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty, 0)
                    continue
                o = active_orders[combined]
                send_resp('CAN', o['order_id'], o['client_id'], side, o['price'], o['qty_original'], 0)
                remove_order(client_id, order_id)
                
            elif action == 'Modify':
                if combined not in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty, 0)
                    continue
                    
                o = active_orders[combined]
                
                if price != 0 and price % 25 != 0:
                    send_resp('REJ', order_id, client_id, side, price, qty, o['qty_remaining'])
                    continue

                old_p = o['price']
                target_p = price if price != 0 else old_p
                new_qty = qty if qty != 0 else o['qty_original']
                executed_qty = o['qty_original'] - o['qty_remaining']
                
                if new_qty < executed_qty:
                    send_resp('REJ', order_id, client_id, side, price, qty, o['qty_remaining'])
                    continue
                    
                qty_diff = new_qty - o['qty_original']
                
                if old_p == target_p:
                    if qty_diff == 0:
                        send_resp('MOD', order_id, client_id, side, target_p, new_qty, new_qty - executed_qty)
                        continue
                    if qty_diff > 0:
                        pass # REQUEUE
                    else:
                        o['qty_remaining'] = new_qty - executed_qty
                        o['qty_original'] = new_qty
                        send_resp('MOD', order_id, client_id, side, target_p, new_qty, new_qty - executed_qty)
                        if o['qty_remaining'] == 0:
                            send_resp('FIL', order_id, client_id, side, target_p, 0, 0)
                            remove_order(client_id, order_id)
                        continue
                        
                # REQUEUE
                send_resp('MOD', order_id, client_id, side, target_p, new_qty, new_qty - executed_qty)
                remove_order(client_id, order_id)
                
                o['qty_remaining'] = new_qty - executed_qty
                o['qty_original'] = new_qty
                o['price'] = target_p
                
                o = match(o)
                if o['qty_remaining'] > 0:
                    add_order(o)
            
            else:
                send_resp('REJ', order_id, client_id, side, price, qty, 0)


    max_p_len = 0
    max_q_len = 0
    print("[ExecutionStdout] Starting execution journal poller...")
    for r in responses:
        p_str = str(r['p'])
        q_str = str(r['q'])
        q_rem_str = str(r['q_rem'])
        if len(p_str) > max_p_len: max_p_len = len(p_str)
        if len(q_str) > max_q_len: max_q_len = len(q_str)
        p_aligned = p_str.rjust(max_p_len)
        q_aligned = q_str.rjust(max_q_len)
        q_rem_aligned = q_rem_str.rjust(max_q_len) # use max_q_len for alignment
        side_str = 'BID' if r['side'] == 'Buy' else 'ASK' if r['side'] == 'Sell' else 'NON'
        print(f"[ExecutionStdout] [{r['exec_type']}] [{side_str}] @ {p_aligned} x {q_aligned} ({q_rem_aligned}) | order_id: {r['order_id']}, client_id: {r['client_id']}, symbol_id: 1")

    book_file = f"{out_dir}/book-core.csv"
    l2_asks = {}
    l2_bids = {}
    for o in active_orders.values():
        if o['qty_remaining'] <= 0: continue
        if o['side'] == 'Buy':
            l2_bids[o['price']] = l2_bids.get(o['price'], 0) + o['qty_remaining']
        else:
            l2_asks[o['price']] = l2_asks.get(o['price'], 0) + o['qty_remaining']
            
    sorted_asks = sorted(l2_asks.items(), key=lambda x: x[0], reverse=True)
    sorted_bids = sorted(l2_bids.items(), key=lambda x: x[0], reverse=True)
    
    with open(book_file, 'w') as f:
        for p, q in sorted_asks:
            f.write(f"A,{p},{q}\n")
        for p, q in sorted_bids:
            f.write(f"B,{p},{q}\n")

    # Dump positions.csv
    with open(f"{out_dir}/positions.csv", 'w') as f:
        for cid in sorted(positions.keys()):
            if positions[cid] != 0:
                f.write(f"{cid},1,{positions[cid]}\n")
            
    # Dump open-orders.csv
    with open(f"{out_dir}/open-orders.csv", 'w') as f:
        for (cid, oid), o in sorted(active_orders.items()):
            if o['qty_remaining'] > 0:
                f.write(f"{cid},{oid},1,{o['side']},{o['price']},{o['qty_original']},{o['qty_remaining']}\n")

if __name__ == '__main__':
    main()
