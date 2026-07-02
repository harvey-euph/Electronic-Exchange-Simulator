#!/usr/bin/env python3
import csv
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: python reference_matcher.py <requests.csv>")
        sys.exit(1)
        
    requests_file = sys.argv[1]
    
    responses = []
    
    active_orders = {} # order_id -> dict
    
    # price levels: price -> list of order_ids
    bids = {} 
    asks = {}
    
    def remove_order(order_id):
        if order_id not in active_orders:
            return
        o = active_orders[order_id]
        book = bids if o['side'] == 'Buy' else asks
        p = o['price']
        if p in book:
            if order_id in book[p]:
                book[p].remove(order_id)
            if not book[p]:
                del book[p]
        del active_orders[order_id]
        
    def add_order(o):
        active_orders[o['order_id']] = o
        book = bids if o['side'] == 'Buy' else asks
        p = o['price']
        if p not in book:
            book[p] = []
        book[p].append(o['order_id'])

    def send_resp(exec_type, order_id, client_id, side, p, q, msg_seq_num=0):
        responses.append({
            'exec_type': exec_type,
            'order_id': order_id,
            'client_id': client_id,
            'side': side,
            'p': p,
            'q': q,
            'msg_seq_num': msg_seq_num
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
                
            maker_id = maker_ids[0]
            maker = active_orders[maker_id]
            
            fill_qty = min(taker['qty_remaining'], maker['qty_remaining'])
            taker['qty_remaining'] -= fill_qty
            maker['qty_remaining'] -= fill_qty
            
            t_exec = 'FIL' if taker['qty_remaining'] == 0 else 'PAR'
            send_resp(t_exec, taker['order_id'], taker['client_id'], taker['side'], best_p, fill_qty)
            
            m_exec = 'FIL' if maker['qty_remaining'] == 0 else 'PAR'
            send_resp(m_exec, maker['order_id'], maker['client_id'], maker['side'], best_p, fill_qty)
            
            if maker['qty_remaining'] == 0:
                remove_order(maker_id)
                
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
            
            if action == 'New':
                if order_id in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty)
                    continue
                
                send_resp('NEW', order_id, client_id, side, price, qty)
                
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
                if order_id not in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty)
                    continue
                o = active_orders[order_id]
                send_resp('CAN', o['order_id'], o['client_id'], side, o['price'], qty)
                remove_order(order_id)
                
            elif action == 'Modify':
                if order_id not in active_orders:
                    send_resp('REJ', order_id, client_id, side, price, qty)
                    continue
                    
                o = active_orders[order_id]
                old_p = o['price']
                target_p = price if price != 0 else old_p
                new_qty = qty if qty != 0 else o['qty_original']
                executed_qty = o['qty_original'] - o['qty_remaining']
                
                if new_qty < executed_qty:
                    send_resp('REJ', order_id, client_id, side, price, qty)
                    continue
                    
                qty_diff = new_qty - o['qty_original']
                
                if old_p == target_p:
                    if qty_diff == 0:
                        send_resp('MOD', order_id, client_id, side, target_p, new_qty)
                        continue
                    if qty_diff > 0:
                        pass # REQUEUE
                    else:
                        o['qty_remaining'] = new_qty - executed_qty
                        o['qty_original'] = new_qty
                        send_resp('MOD', order_id, client_id, side, target_p, new_qty)
                        if o['qty_remaining'] == 0:
                            send_resp('FIL', order_id, client_id, side, target_p, 0)
                            remove_order(order_id)
                        continue
                        
                # REQUEUE
                send_resp('MOD', order_id, client_id, side, target_p, new_qty)
                remove_order(order_id)
                
                o['qty_remaining'] = new_qty - executed_qty
                o['qty_original'] = new_qty
                o['price'] = target_p
                
                o = match(o)
                if o['qty_remaining'] > 0:
                    add_order(o)

    max_p_len = 0
    max_q_len = 0
    print("[ExecutionStdout] Starting execution journal poller...")
    for r in responses:
        p_str = str(r['p'])
        q_str = str(r['q'])
        if len(p_str) > max_p_len: max_p_len = len(p_str)
        if len(q_str) > max_q_len: max_q_len = len(q_str)
        p_aligned = p_str.rjust(max_p_len)
        q_aligned = q_str.rjust(max_q_len)
        side_str = 'BID' if r['side'] == 'Buy' else 'ASK' if r['side'] == 'Sell' else 'NON'
        print(f"[ExecutionStdout] [{r['exec_type']}] [{side_str}] @ {p_aligned} x {q_aligned} | order_id: {r['order_id']}, client_id: {r['client_id']}, symbol_id: 1, msg_seq_num: {r['msg_seq_num']}")

    if len(sys.argv) >= 3:
        book_file = sys.argv[2]
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

if __name__ == '__main__':
    main()
