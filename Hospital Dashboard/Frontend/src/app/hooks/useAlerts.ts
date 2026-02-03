import { useState, useEffect, useCallback, useRef } from 'react';
import { getActiveAlerts, resolveAlert as apiResolveAlert, Alert } from '@/app/api/alerts';

export function useAlerts(pollInterval: number = 10000) {
  const [alerts, setAlerts] = useState<Alert[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  // Fetch active alerts
  const fetchAlerts = useCallback(async () => {
    try {
      setError(null);
      const data = await getActiveAlerts(50);
      setAlerts(data);
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to fetch alerts';
      setError(message);
      console.error('Failed to fetch alerts:', err);
    } finally {
      setLoading(false);
    }
  }, []);

  // Initial fetch and polling
  useEffect(() => {
    fetchAlerts();

    if (pollInterval > 0) {
      pollRef.current = setInterval(fetchAlerts, pollInterval);
    }

    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current);
      }
    };
  }, [fetchAlerts, pollInterval]);

  // Resolve an alert
  const resolveAlert = useCallback(async (id: string) => {
    try {
      setError(null);
      await apiResolveAlert(id);
      setAlerts(prev => prev.filter(a => a._id !== id));
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to resolve alert';
      setError(message);
      console.error('Failed to resolve alert:', err);
      throw err;
    }
  }, []);

  // Manual refresh
  const refresh = useCallback(() => {
    setLoading(true);
    fetchAlerts();
  }, [fetchAlerts]);

  // Get count by level
  const alertCounts = {
    total: alerts.length,
    critical: alerts.filter(a => a.level === 'critical').length,
    error: alerts.filter(a => a.level === 'error').length,
    warning: alerts.filter(a => a.level === 'warning').length,
    info: alerts.filter(a => a.level === 'info').length,
  };

  return {
    alerts,
    alertCounts,
    loading,
    error,
    resolveAlert,
    refresh,
  };
}
