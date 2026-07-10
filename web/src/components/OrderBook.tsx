import React, { useState, useEffect, useRef } from 'react';
import { Side } from '../fbs/exchange/side';
import { formatPrice } from '../types';

interface OrderBookProps {
  symbolId: string;
  onSymbolChange: (v: string) => void;
  bids: { price: bigint; quantity: bigint }[];
  asks: { price: bigint; quantity: bigint }[];
  onPriceClick?: (price: string, side: Side, peggedLevel?: number | null) => void;
  onMidPriceClick?: () => void;
  onReconnectL2?: () => void;
  priceExp?: number;
  symbolInfos?: Map<number, any>;
}

export const OrderBook: React.FC<OrderBookProps> = ({ symbolId, onSymbolChange, bids, asks, onPriceClick, onMidPriceClick, onReconnectL2, priceExp, symbolInfos }) => {
  const [midColor, setMidColor] = useState('var(--text-primary)');
  const prevMidRef = useRef<bigint | null>(null);

  const handleSelectChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    onSymbolChange(e.target.value);
  };

  // Take 5 best asks (lowest prices). Since asks is [High ... Low], we take the last 5.
  const displayAsks = asks.slice(-5);
  const paddedAsks = [...Array(Math.max(0, 5 - displayAsks.length)).fill(null), ...displayAsks];

  // Take 5 best bids (highest prices). Since bids is [High ... Low], we take the first 5.
  const displayBids = bids.slice(0, 5);
  const paddedBids = [...displayBids, ...Array(Math.max(0, 5 - displayBids.length)).fill(null)];

  const isCash = symbolId === '0';

  const bestBid = bids[0]?.price;
  const bestAsk = asks[asks.length - 1]?.price;
  const currentMid = (bestBid !== undefined && bestAsk !== undefined) ? (bestBid + bestAsk) / 2n : null;

  useEffect(() => {
    if (currentMid !== null && prevMidRef.current !== null) {
      if (currentMid > prevMidRef.current) {
        setMidColor('var(--accent-green)');
      } else if (currentMid < prevMidRef.current) {
        setMidColor('var(--accent-red)');
      }
    }
    prevMidRef.current = currentMid;
  }, [currentMid]);

  return (
    <div className="modern-card order-book-container">
      <div className="block-header">
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          <button className="reconnect-btn-modern" onClick={onReconnectL2} title="Reconnect L2">↻</button>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Symbol:</span>
          <select 
            className="modern-select"
            value={symbolId} 
            onChange={handleSelectChange} 
            style={{ padding: '2px 24px 2px 8px', height: '24px', fontSize: '12px' }} 
          >
            <option value="1">BTC/USD (1)</option>
            <option value="2">ETH/USD (2)</option>
            <option value="3">SOL/USD (3)</option>
            {symbolInfos && Array.from(symbolInfos.values()).filter(i => ![0,1,2,3].includes(i.symbolId)).map(info => (
              <option key={info.symbolId} value={info.symbolId.toString()}>
                {info.name} ({info.symbolId})
              </option>
            ))}
          </select>
        </div>
      </div>
      
      <div className="custom-scroll">
        {isCash ? (
          <div style={{ color: 'var(--text-secondary)', textAlign: 'center', marginTop: '20px', fontSize: '12px' }}>
            Cash has no orderbook.
          </div>
        ) : (
          <table className="modern-table" style={{ tableLayout: 'fixed', width: '100%' }}>
            <thead>
              <tr>
                <th style={{ width: '60px' }}>Level</th>
                <th style={{ textAlign: 'right' }}>Price</th>
                <th style={{ width: '80px', textAlign: 'right' }}>Size</th>
              </tr>
            </thead>
            <tbody>
              {paddedAsks.map((level, i) => {
                const levelNum = 5 - i;
                return (
                  <tr 
                    key={`ask-${i}`} 
                    style={{ height: '22px' }}
                  >
                    <td style={{ padding: '2px 0' }}>
                      <button 
                        className="modern-button btn-sell" 
                        style={{ 
                          fontSize: '9px', 
                          padding: '1px 4px', 
                          width: '100%', 
                          opacity: level ? 1 : 0.3,
                          height: '18px'
                        }}
                        disabled={!level}
                        onClick={() => level && onPriceClick?.(formatPrice(level.price, priceExp), Side.Sell, levelNum)}
                      >
                        ASK {levelNum}
                      </button>
                    </td>
                    <td 
                      style={{ 
                        textAlign: 'right', 
                        color: level ? 'var(--text-primary)' : 'var(--border-color)',
                        cursor: level ? 'pointer' : 'default'
                      }}
                      onClick={() => level && onPriceClick?.(formatPrice(level.price, priceExp), Side.Sell, null)}
                    >
                      {level ? formatPrice(level.price, priceExp) : '-'}
                    </td>
                    <td style={{ textAlign: 'right', color: level ? 'var(--text-secondary)' : 'var(--border-color)' }}>
                      {level ? level.quantity.toString() : '-'}
                    </td>
                  </tr>
                );
              })}
              
              <tr style={{ height: '36px', borderTop: '1px solid var(--border-color)', borderBottom: '1px solid var(--border-color)' }}>
                <td colSpan={3} 
                  onClick={onMidPriceClick}
                  style={{ 
                    textAlign: 'center', 
                    color: midColor, 
                    fontSize: '18px', 
                    verticalAlign: 'middle', 
                    fontWeight: 700,
                    transition: 'color 0.2s ease',
                    cursor: onMidPriceClick ? 'pointer' : 'default'
                  }}>
                  {currentMid !== null ? formatPrice(currentMid, priceExp) : '-'}
                </td>
              </tr>

              {paddedBids.map((level, i) => {
                const levelNum = i + 1;
                return (
                  <tr 
                    key={`bid-${i}`} 
                    style={{ height: '22px' }}
                  >
                    <td style={{ padding: '2px 0' }}>
                      <button 
                        className="modern-button btn-buy" 
                        style={{ 
                          fontSize: '9px', 
                          padding: '1px 4px', 
                          width: '100%', 
                          opacity: level ? 1 : 0.3,
                          height: '18px'
                        }}
                        disabled={!level}
                        onClick={() => level && onPriceClick?.(formatPrice(level.price, priceExp), Side.Buy, levelNum)}
                      >
                        BID {levelNum}
                      </button>
                    </td>
                    <td 
                      style={{ 
                        textAlign: 'right', 
                        color: level ? 'var(--text-primary)' : 'var(--border-color)',
                        cursor: level ? 'pointer' : 'default'
                      }}
                      onClick={() => level && onPriceClick?.(formatPrice(level.price, priceExp), Side.Buy, null)}
                    >
                      {level ? formatPrice(level.price, priceExp) : '-'}
                    </td>
                    <td style={{ textAlign: 'right', color: level ? 'var(--text-secondary)' : 'var(--border-color)' }}>
                      {level ? level.quantity.toString() : '-'}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
};
