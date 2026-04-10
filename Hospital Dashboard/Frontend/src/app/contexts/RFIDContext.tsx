import { createContext, useContext, useEffect, useCallback, useRef, ReactNode } from 'react';
import { useSerialRFID, SerialStatus } from '@/app/hooks/useSerialRFID';
import { Radio, Loader2, X } from 'lucide-react';

interface RFIDContextType {
  status: SerialStatus;
  lastUID: string | null;
  isSupported: boolean;
  error: string | null;
  onCardRead: (callback: (uid: string) => void) => () => void;
}

const RFIDContext = createContext<RFIDContextType | null>(null);

export function useRFIDContext() {
  const context = useContext(RFIDContext);
  if (!context) {
    throw new Error('useRFIDContext must be used within RFIDProvider');
  }
  return context;
}

interface RFIDProviderProps {
  children: ReactNode;
}

export function RFIDProvider({ children }: RFIDProviderProps) {
  const cardReadCallbacksRef = useRef<Set<(uid: string) => void>>(new Set());

  const handleCardRead = useCallback((uid: string) => {
    cardReadCallbacksRef.current.forEach(callback => callback(uid));
  }, []);

  const {
    status,
    lastUID,
    isSupported,
    error,
    disconnect
  } = useSerialRFID({
    onCardRead: handleCardRead
  });

  const onCardRead = useCallback((callback: (uid: string) => void) => {
    console.log('[RFIDContext] Subscribing callback');
    cardReadCallbacksRef.current.add(callback);

    return () => {
      console.log('[RFIDContext] Unsubscribing callback');
      cardReadCallbacksRef.current.delete(callback);
    };
  }, []);

  useEffect(() => {
    return () => {
      if (status === 'connected') {
        disconnect();
      }
    };
  }, []);

  return (
    <RFIDContext.Provider value={{ status, lastUID, isSupported, error, onCardRead }}>
      {children}

      {}
      {status !== 'disconnected' && (
        <div className="fixed bottom-4 right-4 z-50">
          <div className={`flex items-center gap-2 px-3 py-2 rounded-full shadow-lg text-sm font-medium ${
            status === 'connected'
              ? 'bg-green-100 text-green-700 dark:bg-green-900 dark:text-green-300'
              : status === 'connecting'
              ? 'bg-yellow-100 text-yellow-700 dark:bg-yellow-900 dark:text-yellow-300'
              : 'bg-red-100 text-red-700 dark:bg-red-900 dark:text-red-300'
          }`}>
            {status === 'connected' ? (
              <>
                <Radio className="w-4 h-4 animate-pulse" />
                <span>RFID Ready {lastUID ? `(${lastUID})` : ''}</span>
              </>
            ) : status === 'connecting' ? (
              <>
                <Loader2 className="w-4 h-4 animate-spin" />
                <span>Connecting...</span>
              </>
            ) : (
              <>
                <X className="w-4 h-4" />
                <span>RFID Error: {error}</span>
              </>
            )}
          </div>
        </div>
      )}
    </RFIDContext.Provider>
  );
}
