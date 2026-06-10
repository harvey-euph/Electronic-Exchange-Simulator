import { Side } from './fbs/exchange/side';

export interface OrderData {
  orderId: string;
  symbolId: number;
  side: Side;
  p: bigint;
  q: bigint;
  filled: bigint;
}

export interface PositionLot {
  price: bigint;
  quantity: bigint;
  timestamp: number;
  orderId: string;
}

export interface SymbolPosition {
  symbolId: number;
  side: Side;
  lots: PositionLot[];
  totalQuantity: bigint;
  averagePrice: bigint;
  realizedPnL: bigint;
}

export interface ConnectedState {
  mgmt: boolean;
  mgmtReady: boolean;
  l2: boolean;
}
