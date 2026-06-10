import React, { useState, useCallback, useEffect, useRef } from 'react';

interface LogViewerProps {
  title: string;
  messages: string[];
  show: boolean;
  onClose: () => void;
  color: string;
  logRef: React.RefObject<HTMLDivElement | null>;
}

export const LogViewer: React.FC<LogViewerProps> = ({ title, messages, show, onClose, color, logRef }) => {
  const [size, setSize] = useState({ width: 400, height: 300 });
  const isResizing = useRef<string | null>(null);
  const startPos = useRef({ x: 0, y: 0 });
  const startSize = useRef({ width: 0, height: 0 });

  const isMgmt = title.includes('Management');

  const onMouseDown = useCallback((type: string) => (e: React.MouseEvent) => {
    isResizing.current = type;
    startPos.current = { x: e.clientX, y: e.clientY };
    startSize.current = { width: size.width, height: size.height };
    e.preventDefault();
  }, [size]);

  useEffect(() => {
    const onMouseMove = (e: MouseEvent) => {
      if (!isResizing.current) return;
      const deltaX = e.clientX - startPos.current.x;
      const deltaY = startPos.current.y - e.clientY; // resizing from bottom, but height grows upwards

      let newWidth = startSize.current.width;
      let newHeight = startSize.current.height;

      if (isResizing.current.includes('right')) {
        newWidth = startSize.current.width + deltaX;
      } else if (isResizing.current.includes('left')) {
        newWidth = startSize.current.width - deltaX;
      }

      if (isResizing.current.includes('top')) {
        newHeight = startSize.current.height + deltaY;
      }

      setSize({
        width: Math.max(200, newWidth),
        height: Math.max(100, newHeight)
      });
    };

    const onMouseUp = () => {
      isResizing.current = null;
    };

    if (show) {
      window.addEventListener('mousemove', onMouseMove);
      window.addEventListener('mouseup', onMouseUp);
    }
    return () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    };
  }, [show]);

  if (!show) return null;

  const positionStyle: React.CSSProperties = isMgmt 
    ? { right: '24px' } 
    : { left: '24px' };

  return (
    <div style={{ 
      position: 'fixed', 
      bottom: '60px', 
      ...positionStyle, 
      width: `${size.width}px`, 
      height: `${size.height}px`, 
      backgroundColor: 'var(--bg-card)', 
      border: '1px solid var(--border-color)', 
      borderRadius: '8px', 
      display: 'flex', 
      flexDirection: 'column', 
      zIndex: 1000, 
      boxShadow: 'var(--shadow-soft)',
      fontFamily: 'var(--font-mono)',
      fontSize: '11px',
      overflow: 'hidden'
    }}>
      {/* Edge Resizers */}
      <div 
        onMouseDown={onMouseDown(isMgmt ? 'left' : 'right')}
        style={{ 
          position: 'absolute', 
          top: 0, 
          bottom: 0, 
          width: '6px', 
          [isMgmt ? 'left' : 'right']: '-3px', 
          cursor: 'ew-resize', 
          zIndex: 1001 
        }} 
      />
      <div 
        onMouseDown={onMouseDown('top')}
        style={{ 
          position: 'absolute', 
          top: '-3px', 
          left: 0, 
          right: 0, 
          height: '6px', 
          cursor: 'ns-resize', 
          zIndex: 1001 
        }}
      />

      <div style={{ 
        padding: '8px 12px', 
        backgroundColor: 'var(--bg-input)', 
        borderBottom: '1px solid var(--border-color)',
        display: 'flex', 
        justifyContent: 'space-between', 
        alignItems: 'center', 
        userSelect: 'none',
        color: 'var(--text-primary)',
        textTransform: 'uppercase',
        letterSpacing: '0.5px',
        fontWeight: 600,
        fontSize: '10px'
      }}>
        <span>{title}</span>
        <button onClick={onClose} style={{ 
          background: 'none', 
          border: 'none', 
          color: 'var(--text-secondary)', 
          cursor: 'pointer',
          padding: '4px',
          fontSize: '16px',
          lineHeight: '1',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          borderRadius: '4px'
        }} className="reconnect-btn-modern">×</button>
      </div>
      <div ref={logRef} className="custom-scroll" style={{ 
        flex: 1, 
        overflowY: 'auto', 
        padding: '12px',
        lineHeight: '1.5',
        backgroundColor: 'rgba(0,0,0,0.2)'
      }}>
        {messages.map((msg, i) => (
          <div key={i} style={{ marginBottom: '2px', whiteSpace: 'pre-wrap', color }}>{msg}</div>
        ))}
        {messages.length === 0 && (
          <div style={{ color: 'var(--text-secondary)', fontStyle: 'italic' }}>Waiting for logs...</div>
        )}
      </div>
    </div>
  );
};
