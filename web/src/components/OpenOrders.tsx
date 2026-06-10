import React, { useState, useEffect, useMemo } from 'react';
import { Side } from '../fbs/exchange/side';
import type { OrderData } from '../types';
import { NumericInput } from './NumericInput';

interface OpenOrdersProps {
  orders: OrderData[];
  onModify: (order: OrderData, newPrice: string, newQty: string) => void;
  onCancel: (order: OrderData) => void;
  currentSymbolId?: string;
  noWrapper?: boolean;
  expandedSymbols: Set<number>;
  onToggleSymbol: (sid: number) => void;
}

export const OpenOrders: React.FC<OpenOrdersProps> = ({
  orders, onModify, onCancel, currentSymbolId, noWrapper,
  expandedSymbols, onToggleSymbol
}) => {
  const [editValues, setEditValues] = useState<Record<string, { p: string, q: string }>>({});
  const lastOrdersRef = React.useRef<Map<string, { p: string, q: string }>>(new Map());

  // Group orders by symbolId
  const ordersBySymbol = useMemo(() => {
    const groups: Record<number, OrderData[]> = {};
    orders.forEach(o => {
      if (!groups[o.symbolId]) groups[o.symbolId] = [];
      groups[o.symbolId].push(o);
    });
    return groups;
  }, [orders]);

  const sortedSymbols = useMemo(() => {
    return Object.keys(ordersBySymbol).map(Number).sort((a, b) => a - b);
  }, [ordersBySymbol]);

  // Initialize or update edit values when orders change
  useEffect(() => {
    setEditValues(prev => {
      const next = { ...prev };
      orders.forEach(o => {
        const orderId = o.orderId;
        const pStr = o.p.toString();
        const qStr = o.q.toString();
        const last = lastOrdersRef.current.get(orderId);

        if (!next[orderId] || (last && (last.p !== pStr || last.q !== qStr))) {
          next[orderId] = { p: pStr, q: qStr };
        }
        lastOrdersRef.current.set(orderId, { p: pStr, q: qStr });
      });

      const currentIds = new Set(orders.map(o => o.orderId));
      Object.keys(next).forEach(id => {
        if (!currentIds.has(id)) {
          delete next[id];
          lastOrdersRef.current.delete(id);
        }
      });

      return next;
    });
  }, [orders]);

  const handleUpdate = (orderId: string, field: 'p' | 'q', val: string) => {
    setEditValues(prev => ({
      ...prev,
      [orderId]: { ...prev[orderId], [field]: val }
    }));
  };

  const handleKeyDown = (e: React.KeyboardEvent, order: OrderData) => {
    if (e.key === 'Enter') {
      const vals = editValues[order.orderId];
      if (vals) {
        onModify(order, vals.p, vals.q);
      }
      handleRevert(order.orderId, order);
      (e.target as HTMLInputElement).blur();
    } else if (e.key === 'Escape') {
      handleRevert(order.orderId, order);
      (e.target as HTMLInputElement).blur();
    }
  };

  const handleRevert = (orderId: string, order: OrderData) => {
    setEditValues(prev => ({
      ...prev,
      [orderId]: { p: order.p.toString(), q: order.q.toString() }
    }));
  };

  const content = (
    <>
      <div className="table-container custom-scroll">
        {sortedSymbols.length === 0 ? (
          <div style={{ textAlign: 'center', padding: '40px', color: 'var(--text-secondary)', fontSize: '13px' }}>
            No open orders
          </div>
        ) : (
          <table className="modern-table" style={{ tableLayout: 'fixed' }}>
            <thead>
              <tr>
                <th style={{ width: '70px', textAlign: 'right' }}>Order ID</th>
                <th style={{ width: '30px', textAlign: 'right' }}>Side</th>
                <th style={{ width: '65px', textAlign: 'right' }}>Price</th>
                <th style={{ width: '65px', textAlign: 'right' }}>Qty</th>
                <th style={{ width: '45px', textAlign: 'right' }}>Fill</th>
                <th style={{ width: '55px', textAlign: 'right' }}></th>
              </tr>
            </thead>
            <tbody>
              {sortedSymbols.map(sid => (
                <React.Fragment key={sid}>
                  <tr className="symbol-group-header" onClick={() => onToggleSymbol(sid)}>
                    <td colSpan={6}>
                      <div className="symbol-group-title">
                        <span className={`expand-icon ${expandedSymbols.has(sid) ? 'expanded' : ''}`}>▼</span>
                        Symbol {sid} ({ordersBySymbol[sid].length})
                      </div>
                    </td>
                  </tr>
                  {expandedSymbols.has(sid) && ordersBySymbol[sid].map((o, index) => {
                    const vals = editValues[o.orderId] || { p: o.p.toString(), q: o.q.toString() };
                    const isModified = vals.p !== o.p.toString() || vals.q !== o.q.toString();
                    const displayId = `${o.orderId}`;
                    
                    return (
                      <tr 
                        key={o.orderId} 
                        style={{ 
                          borderBottom: '1px solid var(--border-color)',
                          backgroundColor: index % 2 === 0 ? 'transparent' : 'rgba(255,255,255,0.02)'
                        }}
                      >
                        <td style={{ textAlign: 'right', color: 'var(--text-secondary)', fontSize: '10px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} title={o.orderId}>{displayId}</td>
                        <td style={{ 
                          textAlign: 'right',
                          color: o.side === Side.Buy ? 'var(--accent-green)' : o.side === Side.Sell ? 'var(--accent-red)' : 'var(--text-secondary)',
                          fontWeight: 600,
                          fontSize: '10px'
                        }}>
                          {Side[o.side]}
                        </td>
                        <td style={{ textAlign: 'right' }}>
                          <NumericInput 
                            className="editable-cell-input"
                            value={vals.p} 
                            onChange={(v) => handleUpdate(o.orderId, 'p', v)}
                            onKeyDown={(e) => handleKeyDown(e, o)}
                            onBlur={() => handleRevert(o.orderId, o)}
                            style={{ 
                              width: '70%', 
                              height: '22px', 
                              textAlign: 'right', 
                              fontSize: '11px',
                              border: isModified ? '1px solid var(--accent-blue)' : '1px solid transparent',
                              padding: '0 4px'
                            }} 
                          />
                        </td>
                        <td style={{ textAlign: 'right' }}>
                          <NumericInput 
                            className="editable-cell-input"
                            value={vals.q} 
                            onChange={(v) => handleUpdate(o.orderId, 'q', v)}
                            onKeyDown={(e) => handleKeyDown(e, o)}
                            onBlur={() => handleRevert(o.orderId, o)}
                            style={{ 
                              width: '70%', 
                              height: '22px', 
                              textAlign: 'right', 
                              fontSize: '11px',
                              border: isModified ? '1px solid var(--accent-blue)' : '1px solid transparent',
                              padding: '0 4px'
                            }} 
                          />
                        </td>
                        <td style={{ textAlign: 'right', color: 'var(--text-secondary)', fontSize: '11px' }}>{o.filled.toString()}</td>
                        <td style={{ textAlign: 'right' }}>
                          <button 
                            className="modern-button btn-sell" 
                            onClick={() => onCancel(o)}
                            style={{ padding: '2px 4px', fontSize: '10px', height: '22px', minWidth: '45px' }}
                          >
                            Cancel
                          </button>
                        </td>
                      </tr>
                    );
                  })}
                </React.Fragment>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </>
  );

  if (noWrapper) return content;

  return (
    <div className="modern-card open-orders-section">
      {content}
    </div>
  );
};
