import { useState, useCallback, useRef, useEffect } from 'react';

declare global {
  interface Navigator {
    serial: Serial;
  }

  interface Serial {
    requestPort(options?: SerialPortRequestOptions): Promise<SerialPort>;
    getPorts(): Promise<SerialPort[]>;
  }

  interface SerialPortRequestOptions {
    filters?: SerialPortFilter[];
  }

  interface SerialPortFilter {
    usbVendorId?: number;
    usbProductId?: number;
  }

  interface SerialPort {
    readable: ReadableStream<Uint8Array> | null;
    writable: WritableStream<Uint8Array> | null;
    open(options: SerialOptions): Promise<void>;
    close(): Promise<void>;
  }

  interface SerialOptions {
    baudRate: number;
    dataBits?: number;
    stopBits?: number;
    parity?: 'none' | 'even' | 'odd';
    bufferSize?: number;
    flowControl?: 'none' | 'hardware';
  }
}

export interface UseSerialRFIDOptions {
  baudRate?: number;
  onCardRead?: (uid: string) => void;
  onError?: (error: string) => void;
  onStatusChange?: (status: SerialStatus) => void;
}

export type SerialStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

export interface UseSerialRFIDReturn {
  status: SerialStatus;
  lastUID: string | null;
  error: string | null;
  isSupported: boolean;
  connect: () => Promise<boolean>;
  disconnect: () => Promise<void>;
  sendCommand: (command: string) => Promise<void>;
}

export function useSerialRFID(options: UseSerialRFIDOptions = {}): UseSerialRFIDReturn {
  const {
    baudRate = 9600,
    onCardRead,
    onError,
    onStatusChange
  } = options;

  const [status, setStatus] = useState<SerialStatus>('disconnected');
  const [lastUID, setLastUID] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const portRef = useRef<SerialPort | null>(null);
  const readerRef = useRef<ReadableStreamDefaultReader<string> | null>(null);
  const isReadingRef = useRef(false);

  const isSupported = typeof navigator !== 'undefined' && 'serial' in navigator;

  const updateStatus = useCallback((newStatus: SerialStatus) => {
    setStatus(newStatus);
    onStatusChange?.(newStatus);
  }, [onStatusChange]);

  const handleError = useCallback((errorMsg: string) => {
    setError(errorMsg);
    onError?.(errorMsg);
    updateStatus('error');
  }, [onError, updateStatus]);

  const onCardReadRef = useRef(onCardRead);
  useEffect(() => {
    onCardReadRef.current = onCardRead;
  }, [onCardRead]);

  const parseSerialData = useCallback((data: string) => {
    const lines = data.split('\n');

    console.log('[RFID] Received raw data:', JSON.stringify(data));

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed) continue;

      console.log('[RFID] Processing line:', JSON.stringify(trimmed));

      if (trimmed.startsWith('RFID:')) {
        const uid = trimmed.substring(5);
        if (uid.length >= 8) {
          console.log('[RFID] Card detected (with prefix):', uid);
          setLastUID(uid);
          onCardReadRef.current?.(uid);
        }
      }

      else if (/^[0-9A-Fa-f]{8,}$/.test(trimmed)) {
        const uid = trimmed.toUpperCase();
        console.log('[RFID] Card detected (raw hex):', uid);
        setLastUID(uid);
        onCardReadRef.current?.(uid);
      }
      else if (trimmed === 'RFID_READY') {
        console.log('[RFID] Device ready');
      } else if (trimmed === 'PONG') {
        console.log('[RFID] Device responded to ping');
      } else if (trimmed.startsWith('ERROR:')) {
        handleError(trimmed.substring(6));
      } else if (trimmed.startsWith('MFRC522_VERSION:')) {
        console.log('[RFID] Module version:', trimmed.substring(16));
      }
    }
  }, [onCardRead, handleError]);

  const readLoop = useCallback(async () => {
    if (!portRef.current || !portRef.current.readable) return;

    const textDecoder = new TextDecoderStream();

    const readableStreamClosed = portRef.current.readable.pipeTo(textDecoder.writable as unknown as WritableStream<Uint8Array>);
    readerRef.current = textDecoder.readable.getReader();

    isReadingRef.current = true;
    let buffer = '';

    try {
      while (isReadingRef.current) {
        const { value, done } = await readerRef.current.read();

        if (done) {
          break;
        }

        if (value) {
          buffer += value;

          const lines = buffer.split('\n');
          if (lines.length > 1) {

            for (let i = 0; i < lines.length - 1; i++) {
              parseSerialData(lines[i] + '\n');
            }

            buffer = lines[lines.length - 1];
          }
        }
      }
    } catch (err) {
      if (isReadingRef.current) {
        handleError(`Read error: ${err}`);
      }
    } finally {
      readerRef.current?.releaseLock();
      await readableStreamClosed.catch(() => {});
    }
  }, [parseSerialData, handleError]);

  const connect = useCallback(async (): Promise<boolean> => {
    if (!isSupported) {
      handleError('Web Serial API is not supported in this browser');
      return false;
    }

    try {
      updateStatus('connecting');
      setError(null);

      const port = await navigator.serial.requestPort({
        filters: [
          { usbVendorId: 0x2341 },
          { usbVendorId: 0x1A86 },
          { usbVendorId: 0x0403 },
          { usbVendorId: 0x10C4 },
        ]
      });

      await port.open({ baudRate });
      portRef.current = port;

      updateStatus('connected');

      readLoop();

      setTimeout(() => {
        sendCommand('PING');
      }, 500);

      return true;
    } catch (err) {
      if ((err as Error).name === 'NotFoundError') {
        handleError('No compatible serial device found. Please connect Arduino.');
      } else {
        handleError(`Connection failed: ${err}`);
      }
      return false;
    }
  }, [isSupported, baudRate, updateStatus, handleError, readLoop]);

  const disconnect = useCallback(async () => {
    isReadingRef.current = false;

    if (readerRef.current) {
      try {
        await readerRef.current.cancel();
      } catch (err) {
        console.warn('[RFID] Error canceling reader:', err);
      }
      readerRef.current = null;
    }

    if (portRef.current) {
      try {
        await portRef.current.close();
      } catch (err) {
        console.warn('[RFID] Error closing port:', err);
      }
      portRef.current = null;
    }

    updateStatus('disconnected');
    setLastUID(null);
    setError(null);
  }, [updateStatus]);

  const sendCommand = useCallback(async (command: string) => {
    if (!portRef.current || !portRef.current.writable) {
      console.warn('[RFID] Cannot send command: port not available');
      return;
    }

    const writer = portRef.current.writable.getWriter();
    const encoder = new TextEncoder();

    try {
      await writer.write(encoder.encode(command + '\n'));
    } finally {
      writer.releaseLock();
    }
  }, []);

  useEffect(() => {
    return () => {
      disconnect();
    };
  }, [disconnect]);

  return {
    status,
    lastUID,
    error,
    isSupported,
    connect,
    disconnect,
    sendCommand
  };
}
