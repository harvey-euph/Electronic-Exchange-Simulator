import React, { useMemo } from 'react';
import type { SymbolPosition } from '../types';
import { Side } from '../fbs/exchange/side';
import { formatPrice, getPriceExpForSymbol } from '../types';

interface PositionsProps {
  positions: [number, SymbolPosition][];
  prices: Map<number, bigint>;
  currentSymbolId?: string;
  onFlatten?: (symbolId: number, side: Side, quantity: bigint) => void;
  noWrapper?: boolean;
  expandedSymbols: Set<number>;
  onToggleSymbol: (sid: number) => void;
}

export const Positions: React.FC<PositionsProps> = ({ 
  positions, prices, onFlatten, noWrapper,
  expandedSymbols, onToggleSymbol
}) => {
  const activePositions = useMemo(() => 
    positions.filter(([, p]) => p.totalQuantity !== 0n || p.realizedPnL !== 0n)
      .sort((a, b) => a[0] - b[0]),
  [positions]);

  const content = (
    <>
      <div className="table-container custom-scroll">
        <table className="modern-table" style={{ tableLayout: 'fixed' }}>
          <thead>
            <tr>
              <th style={{ width: '70px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>Order ID</th>
              <th style={{ width: '40px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>Pos</th>
              <th style={{ width: '48px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}></th>
              <th style={{ width: '48px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>Avg</th>
              <th style={{ width: '48px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>Mark</th>
              <th style={{ width: '53px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>U PnL</th>
              <th style={{ width: '53px', textAlign: 'right', verticalAlign: 'bottom', padding: '0 8px 10px 8px' }}>R PnL</th>
            </tr>
          </thead>
          <tbody>
            {activePositions.map(([sId, pos]) => {
              const exp = getPriceExpForSymbol(sId);
              const markPrice = prices.get(sId) || pos.averagePrice || 0n;
              const unrealizedPnL = pos.side === Side.Buy 
                ? (markPrice - pos.averagePrice) * pos.totalQuantity
                : (pos.averagePrice - markPrice) * pos.totalQuantity;
              
              const displayPos = pos.side === Side.Sell ? -pos.totalQuantity : pos.totalQuantity;
              const isExpanded = expandedSymbols.has(sId);

              // Group lots by orderId
              const groupedLots = [];
              const orderIdMap = new Map();
              
              for (const lot of pos.lots) {
                if (orderIdMap.has(lot.orderId)) {
                  const grouped = orderIdMap.get(lot.orderId);
                  grouped.totalPrice += lot.price * lot.quantity;
                  grouped.quantity += lot.quantity;
                  grouped.timestamp = Math.max(grouped.timestamp, lot.timestamp);
                } else {
                  const grouped = { 
                     orderId: lot.orderId, 
                     quantity: lot.quantity, 
                     totalPrice: lot.price * lot.quantity, 
                     timestamp: lot.timestamp 
                  };
                  orderIdMap.set(lot.orderId, grouped);
                  groupedLots.push(grouped);
                }
              }

              return (
                <React.Fragment key={sId}>
                  <tr 
                    className="symbol-group-header" 
                    onClick={() => onToggleSymbol(sId)}
                    style={{ borderBottom: isExpanded ? 'none' : '1px solid var(--border-color)' }}
                  >
                    <td style={{ textAlign: 'left', verticalAlign: 'middle' }}>
                      <div className="symbol-group-title" style={{ gap: '6px' }}>
                        <span className={`expand-icon ${isExpanded ? 'expanded' : ''}`}>▼</span>
                        Symbol {sId}
                      </div>
                    </td>
                    <td style={{ 
                      textAlign: 'right',
                      color: displayPos > 0n ? 'var(--accent-green)' : displayPos < 0n ? 'var(--accent-red)' : 'var(--text-secondary)',
                      fontSize: '11px',
                      verticalAlign: 'middle'
                    }}>
                      {displayPos.toString()}
                    </td>
                    <td style={{ textAlign: 'center', verticalAlign: 'middle' }}>
                      {pos.totalQuantity !== 0n && onFlatten && (
                        <button 
                          className="modern-button btn-secondary flat-btn" 
                          onClick={(e) => { e.stopPropagation(); onFlatten(sId, pos.side, pos.totalQuantity); }}
                          style={{ padding: '0px 4px', fontSize: '8px', height: '14px', lineHeight: '12px', minWidth: 'auto', whiteSpace: 'nowrap' }}
                        >
                          FLAT ALL
                        </button>
                      )}
                    </td>
                    <td style={{ textAlign: 'right', fontSize: '10px', verticalAlign: 'middle', color: 'var(--text-primary)' }}>
                      {formatPrice(pos.averagePrice, exp)}
                    </td>
                    <td style={{ textAlign: 'right', fontSize: '10px', verticalAlign: 'middle', color: 'var(--text-secondary)' }}>
                      {formatPrice(markPrice, exp)}
                    </td>
                    <td style={{ 
                      textAlign: 'right', 
                      fontSize: '10px', 
                      verticalAlign: 'middle', 
                      color: unrealizedPnL >= 0n ? 'var(--accent-green)' : 'var(--accent-red)' 
                    }}>
                      {unrealizedPnL >= 0n ? '+' : ''}{formatPrice(unrealizedPnL, exp)}
                    </td>
                    <td style={{ 
                      textAlign: 'right', 
                      fontSize: '10px', 
                      verticalAlign: 'middle', 
                      color: pos.realizedPnL >= 0n ? 'var(--text-secondary)' : 'var(--accent-red)',
                      opacity: 0.8 
                    }}>
                      {pos.realizedPnL >= 0n ? '+' : ''}{formatPrice(pos.realizedPnL, exp)}
                    </td>
                  </tr>
                  {isExpanded && groupedLots.map((gl, idx) => {
                    const avgPrice = gl.totalPrice / gl.quantity;
                    return (
                      <tr key={`${gl.orderId}-${idx}`} style={{ backgroundColor: 'rgba(255,255,255,0.02)', borderBottom: idx === groupedLots.length - 1 ? '1px solid var(--border-color)' : 'none' }}>
                        <td style={{ padding: '4px 8px 4px 20px', fontSize: '11px', color: 'var(--text-secondary)' }}>
                          {gl.orderId === 'sync' ? 'SYNC' : gl.orderId}
                        </td>
                        <td style={{ textAlign: 'right', fontSize: '11px', color: 'var(--text-secondary)', padding: '4px 8px' }}>
                          {pos.side === Side.Sell ? '-' : '+'}{gl.quantity.toString()}
                        </td>
                        <td style={{ textAlign: 'center', padding: '4px 8px' }}>
                          {onFlatten && (
                            <button 
                              className="modern-button btn-secondary flat-btn" 
                              onClick={(e) => { e.stopPropagation(); onFlatten(sId, pos.side, gl.quantity); }}
                              style={{ padding: '0px 3px', fontSize: '7px', height: '12px', lineHeight: '10px', minWidth: 'auto', whiteSpace: 'nowrap' }}
                            >
                              FLAT
                            </button>
                          )}
                        </td>
                        <td style={{ textAlign: 'right', fontSize: '10px', color: 'var(--text-secondary)', padding: '4px 8px' }}>
                          {formatPrice(avgPrice, exp)}
                        </td>
                        <td style={{ padding: '4px 8px' }}></td>
                        <td colSpan={2} style={{ textAlign: 'right', fontSize: '10px', color: 'var(--text-secondary)', padding: '4px 8px' }}>
                          <div style={{ display: 'flex', justifyContent: 'flex-end', alignItems: 'center' }}>
                            <span>{new Date(gl.timestamp).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: true })}</span>
                          </div>
                        </td>
                      </tr>
                    );
                  })}
                </React.Fragment>
              );
            })}
          </tbody>
        </table>
        {activePositions.length === 0 && (
          <div style={{ textAlign: 'center', padding: '20px', color: 'var(--text-secondary)', fontSize: '12px' }}>
            No active positions
          </div>
        )}
      </div>
    </>
  );

  if (noWrapper) return content;

  return (
    <div className="modern-card positions-section">
      {content}
    </div>
  );
};
