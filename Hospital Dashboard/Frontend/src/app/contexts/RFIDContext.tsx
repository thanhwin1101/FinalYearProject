import { createContext, useContext, useEffect, useState, useCallback, useRef, ReactNode } from 'react';
import { useSerialRFID, SerialStatus } from '@/app/hooks/useSerialRFID';
import { Usb, Radio, Loader2, X } from 'lucide-react';
import { Button } from '@/app/components/ui/button';

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
  const [showPrompt, setShowPrompt] = useState(false);
  const [hasAttemptedConnect, setHasAttemptedConnect] = useState(false);
  
  // Use ref to store callbacks so handleCardRead always has latest callbacks
  const cardReadCallbacksRef = useRef<Set<(uid: string) => void>>(new Set());

  // Callback when card is read - notify all subscribers
  const handleCardRead = useCallback((uid: string) => {
    console.log('[RFIDContext] Card read:', uid, 'Callbacks:', cardReadCallbacksRef.current.size);
    cardReadCallbacksRef.current.forEach(callback => {
      console.log('[RFIDContext] Calling callback with UID:', uid);
      callback(uid);
    });
  }, []);

  const {
    status,
    lastUID,
    isSupported,
    error,
    connect,
    disconnect
  } = useSerialRFID({
    onCardRead: handleCardRead
  });

  // Show connection prompt on mount (only if supported and not connected)
  useEffect(() => {
    if (isSupported && !hasAttemptedConnect && status === 'disconnected') {
      // Small delay to let the page render first
      const timer = setTimeout(() => {
        setShowPrompt(true);
      }, 500);
      return () => clearTimeout(timer);
    }
  }, [isSupported, hasAttemptedConnect, status]);

  // Handle connect button click
  const handleConnect = async () => {
    setHasAttemptedConnect(true);
    setShowPrompt(false);
    await connect();
  };

  // Handle dismiss
  const handleDismiss = () => {
    setHasAttemptedConnect(true);
    setShowPrompt(false);
  };

  // Subscribe to card read events using ref
  const onCardRead = useCallback((callback: (uid: string) => void) => {
    console.log('[RFIDContext] Subscribing callback');
    cardReadCallbacksRef.current.add(callback);
    // Return unsubscribe function
    return () => {
      console.log('[RFIDContext] Unsubscribing callback');
      cardReadCallbacksRef.current.delete(callback);
    };
  }, []);

  // Cleanup on unmount
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
      
      {/* Connection Prompt Modal */}
      {showPrompt && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-[9999]">
          <div className="bg-white dark:bg-gray-800 rounded-xl shadow-2xl p-6 max-w-md mx-4 animate-in fade-in zoom-in duration-200">
            <div className="flex items-start justify-between mb-4">
              <div className="flex items-center gap-3">
                <div className="w-12 h-12 rounded-full bg-blue-100 dark:bg-blue-900 flex items-center justify-center">
                  <Usb className="w-6 h-6 text-blue-600 dark:text-blue-400" />
                </div>
                <div>
                  <h3 className="text-lg font-semibold">Connect RFID Reader</h3>
                  <p className="text-sm text-gray-500 dark:text-gray-400">Connect once to scan patient cards</p>
                </div>
              </div>
              <button
                onClick={handleDismiss}
                className="text-gray-400 hover:text-gray-600 dark:hover:text-gray-300"
              >
                <X className="w-5 h-5" />
              </button>
            </div>
            
            <div className="bg-gray-50 dark:bg-gray-700/50 rounded-lg p-4 mb-4">
              <p className="text-sm text-gray-600 dark:text-gray-300">
                📌 <strong>Instructions:</strong>
              </p>
              <ol className="text-sm text-gray-600 dark:text-gray-300 mt-2 space-y-1 list-decimal list-inside">
                <li>Plug RFID reader into USB port</li>
                <li>Click "Connect" button below</li>
                <li>Select COM port (Arduino/CH340)</li>
              </ol>
            </div>

            <div className="flex gap-3">
              <Button
                variant="outline"
                onClick={handleDismiss}
                className="flex-1"
              >
                Skip
              </Button>
              <Button
                onClick={handleConnect}
                className="flex-1 bg-blue-600 hover:bg-blue-700"
              >
                <Usb className="w-4 h-4 mr-2" />
                Connect
              </Button>
            </div>
          </div>
        </div>
      )}

      {/* Connection Status Indicator (bottom right corner) */}
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
