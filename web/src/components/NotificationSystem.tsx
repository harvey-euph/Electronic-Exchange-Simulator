import React, { useState, useEffect, useCallback, forwardRef, useImperativeHandle } from 'react';

export type NotificationType = 'acked' | 'rejected' | 'info';

export interface Notification {
  id: string;
  type: NotificationType;
  title: string;
  content: string;
}

export interface NotificationSystemRef {
  addNotification: (type: NotificationType, title: string, content: string) => void;
}

export const NotificationSystem = forwardRef<NotificationSystemRef, {}>((_, ref) => {
  const [notifications, setNotifications] = useState<Notification[]>([]);

  const addNotification = useCallback((type: NotificationType, title: string, content: string) => {
    const id = Math.random().toString(36).substring(2, 9);
    setNotifications(prev => [...prev, { id, type, title, content }]);

    // Auto-remove after 1.5s (0.3s slide in + 0.9s stay + 0.3s fade out)
    setTimeout(() => {
      setNotifications(prev => prev.filter(n => n.id !== id));
    }, 1500);
  }, []);

  useImperativeHandle(ref, () => ({
    addNotification
  }), [addNotification]);

  return (
    <div className="notification-container">
      {notifications.map(n => (
        <div key={n.id} className={`notification-item notif-${n.type}`}>
          <div className={`notification-title notif-title-${n.type}`}>{n.title}</div>
          <div className="notification-content">{n.content}</div>
        </div>
      ))}
    </div>
  );
});
