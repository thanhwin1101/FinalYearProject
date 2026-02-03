import { useState, useCallback } from 'react';
import { 
  getMissions, 
  createMission as apiCreateMission,
  cancelMission as apiCancelMission,
  markMissionArrived,
  markMissionCompleted,
  createDeliveryMission as apiCreateDeliveryMission,
  cancelDeliveryMission as apiCancelDeliveryMission,
  TransportMission,
  CreateMissionData,
  DeliveryMissionRequest,
  DeliveryMissionResponse
} from '@/app/api/missions';

export function useMissions() {
  const [missions, setMissions] = useState<TransportMission[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Fetch missions
  const fetchMissions = useCallback(async (params?: { 
    status?: string; 
    robotId?: string; 
    limit?: number 
  }) => {
    try {
      setLoading(true);
      setError(null);
      const data = await getMissions(params);
      setMissions(data);
      return data;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to fetch missions';
      setError(message);
      console.error('Failed to fetch missions:', err);
      return [];
    } finally {
      setLoading(false);
    }
  }, []);

  // Create new mission
  const createMission = useCallback(async (data: CreateMissionData) => {
    try {
      setError(null);
      const mission = await apiCreateMission(data);
      setMissions(prev => [mission, ...prev]);
      return mission;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to create mission';
      setError(message);
      console.error('Failed to create mission:', err);
      throw err;
    }
  }, []);

  // Cancel mission
  const cancelMission = useCallback(async (missionId: string) => {
    try {
      setError(null);
      const updated = await apiCancelMission(missionId);
      setMissions(prev => prev.map(m => 
        m.missionId === missionId ? updated : m
      ));
      return updated;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to cancel mission';
      setError(message);
      console.error('Failed to cancel mission:', err);
      throw err;
    }
  }, []);

  // Mark mission as arrived
  const markArrived = useCallback(async (missionId: string) => {
    try {
      setError(null);
      const updated = await markMissionArrived(missionId);
      setMissions(prev => prev.map(m => 
        m.missionId === missionId ? updated : m
      ));
      return updated;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to mark mission as arrived';
      setError(message);
      console.error('Failed to mark mission as arrived:', err);
      throw err;
    }
  }, []);

  // Mark mission as completed
  const markCompleted = useCallback(async (missionId: string) => {
    try {
      setError(null);
      const updated = await markMissionCompleted(missionId);
      setMissions(prev => prev.map(m => 
        m.missionId === missionId ? updated : m
      ));
      return updated;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to mark mission as completed';
      setError(message);
      console.error('Failed to mark mission as completed:', err);
      throw err;
    }
  }, []);

  // Get missions by status
  const getActiveMissions = useCallback(() => {
    return missions.filter(m => 
      ['pending', 'en_route', 'arrived'].includes(m.status)
    );
  }, [missions]);

  const getCompletedMissions = useCallback(() => {
    return missions.filter(m => 
      ['completed', 'cancelled'].includes(m.status)
    );
  }, [missions]);

  // Create delivery mission for Carry Robot
  const createDeliveryMission = useCallback(async (data: DeliveryMissionRequest): Promise<DeliveryMissionResponse> => {
    try {
      setError(null);
      const result = await apiCreateDeliveryMission(data);
      // Refresh missions list after creating
      await fetchMissions();
      return result;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to create delivery mission';
      setError(message);
      console.error('Failed to create delivery mission:', err);
      throw err;
    }
  }, [fetchMissions]);

  // Cancel delivery mission
  const cancelDeliveryMission = useCallback(async (missionId: string, cancelledBy?: string) => {
    try {
      setError(null);
      await apiCancelDeliveryMission(missionId, cancelledBy);
      // Update local state
      setMissions(prev => prev.map(m => 
        m.missionId === missionId ? { ...m, status: 'cancelled' as const } : m
      ));
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to cancel delivery mission';
      setError(message);
      console.error('Failed to cancel delivery mission:', err);
      throw err;
    }
  }, []);

  return {
    missions,
    loading,
    error,
    fetchMissions,
    createMission,
    cancelMission,
    markArrived,
    markCompleted,
    getActiveMissions,
    getCompletedMissions,
    createDeliveryMission,
    cancelDeliveryMission,
  };
}
