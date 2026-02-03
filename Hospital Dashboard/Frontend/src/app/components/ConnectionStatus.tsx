import { useState, useEffect } from 'react';
import { Wifi, WifiOff } from 'lucide-react';
import { API_ENDPOINTS } from '@/app/api/config';
import { get } from '@/app/api/http';

interface ConnectionStatusProps {
  className?: string;
}

export function ConnectionStatus({ className = '' }: ConnectionStatusProps) {
  const [isConnected, setIsConnected] = useState(true);
  const [lastCheck, setLastCheck] = useState<Date | null>(null);

  useEffect(() => {
    const checkConnection = async () => {
      try {
        // Simple health check - try to fetch patients meta
        await get(`${API_ENDPOINTS.patients}/meta`);
        setIsConnected(true);
      } catch {
        setIsConnected(false);
      }
      setLastCheck(new Date());
    };

    // Check immediately
    checkConnection();

    // Check every 30 seconds
    const interval = setInterval(checkConnection, 30000);

    return () => clearInterval(interval);
  }, []);

  return (
    <div className={`flex items-center gap-2 ${className}`}>
      {isConnected ? (
        <>
          <Wifi className="w-4 h-4 text-green-500" />
          <span className="text-xs text-green-600">Connected</span>
        </>
      ) : (
        <>
          <WifiOff className="w-4 h-4 text-red-500" />
          <span className="text-xs text-red-600">Disconnected</span>
        </>
      )}
    </div>
  );
}
