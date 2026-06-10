import React, { useEffect, useRef } from 'react';

interface EmbeddedLogProps {
  logs: string[];
  onClear: () => void;
}

export const EmbeddedLog: React.FC<EmbeddedLogProps> = ({ logs, onClear }) => {
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [logs]);

  const formatLogText = (msg: string) => {
    let formatted = msg;
    // Transform [Exec] ID=123 Type=New ... -> [New] ID=123 ...
    if (formatted.includes('[Exec]')) {
      formatted = formatted.replace(/\[Exec\]\s+(ID=[^\s]+)\s+Type=([^\s]+)/, '[$2] $1');
    }
    return formatted;
  };

  const getLogStyle = (msg: string) => {
    const isError = msg.includes('[Error]');
    const isExec = msg.includes('[Exec]') || msg.match(/\[(New|Cancelled|Replaced|Fill|PartialFill|OrderStatus)\]/);
    const isSystem = msg.includes('[System]');

    let borderColor = 'var(--border-color)';
    let bgColor = 'rgba(255, 255, 255, 0.03)';
    let textColor = 'var(--text-secondary)';

    if (isError) {
      borderColor = 'var(--accent-red)';
      bgColor = 'rgba(246, 70, 93, 0.1)';
      textColor = 'var(--accent-red)';
    } else if (isExec) {
      borderColor = 'var(--accent-green)';
      bgColor = 'rgba(14, 203, 129, 0.1)';
      textColor = 'var(--accent-green)';
    } else if (isSystem) {
      borderColor = 'var(--accent-blue)';
      bgColor = 'rgba(86, 156, 214, 0.1)';
      textColor = 'var(--accent-blue)';
    }

    return { borderColor, bgColor, textColor };
  };

  return (
    <div className="modern-card" style={{ height: '240px', display: 'flex', flexDirection: 'column', padding: '12px' }}>
      <div className="block-header" style={{ marginBottom: '12px' }}>
        <h2 className="block-title">Activity Log</h2>
        <button
          className="modern-button btn-secondary"
          onClick={onClear}
          style={{
            padding: '2px 8px',
            fontSize: '10px',
            height: '20px',
            lineHeight: '14px',
            minWidth: 'auto',
            borderRadius: '4px',
            border: '1px solid var(--border-color)',
            cursor: 'pointer'
          }}
        >
          Clear
        </button>
      </div>
      <div 
        ref={scrollRef}
        className="custom-scroll" 
        style={{ 
          flex: 1, 
          overflowY: 'auto', 
          display: 'flex',
          flexDirection: 'column',
          gap: '6px',
          paddingRight: '4px'
        }}
      >
        {logs.map((log, i) => {
          const { borderColor, bgColor, textColor } = getLogStyle(log);
          const displayText = formatLogText(log);
          return (
            <div 
              key={i} 
              style={{ 
                padding: '6px 10px',
                backgroundColor: bgColor,
                borderLeft: `3px solid ${borderColor}`,
                borderRadius: '4px',
                fontSize: '10px',
                fontFamily: 'var(--font-mono)',
                lineHeight: '1.4',
                color: textColor,
                whiteSpace: 'pre-wrap',
                textAlign: 'left'
              }}
            >
              {displayText}
            </div>
          );
        })}
        {logs.length === 0 && (
          <div style={{ color: 'var(--border-color)', fontStyle: 'italic', fontSize: '11px', textAlign: 'center', marginTop: '20px' }}>
            No activity recorded yet
          </div>
        )}
      </div>
    </div>
  );
};
