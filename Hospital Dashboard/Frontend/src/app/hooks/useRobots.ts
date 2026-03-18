import { useState, useEffect, useCallback, useRef } from 'react';
import {
  getCarryRobotsStatus,
  CarryStatusResponse,
  CarryRobotStatus,
} from '@/app/api/robots';
import {
  getMissions,
  cancelMission as apiCancelMission,
  cancelDeliveryMission as apiCancelDeliveryMission,
  TransportMission
} from '@/app/api/missions';
import { Robot, RobotStatus } from '@/app/types/robot';

function mapStatus(backendStatus: string): RobotStatus {
  const statusMap: Record<string, RobotStatus> = {
    'idle': 'Idle',
    'busy': 'Moving',
    'charging': 'Idle',
    'maintenance': 'Error',
    'offline': 'Error',
    'low_battery': 'Error',
  };
  return statusMap[backendStatus] || 'Idle';
}

function carryRobotToFrontend(robot: CarryRobotStatus): Robot {
  return {
    id: robot.robotId,
    type: 'Carry',
    name: robot.name || `Carry-${robot.robotId}`,
    currentLocation: robot.location || 'Unknown',
    destination: robot.destination || '',
    currentNode: robot.currentNode || '',
    status: mapStatus(robot.status),
    taskDescription: robot.carrying !== '—' ? `Carrying: ${robot.carrying}` : undefined,
    batteryLevel: robot.batteryLevel || 0,
    lastUpdated: new Date().toISOString(),
  };
}

export function useRobots(pollInterval: number = 5000) {
  const [robots, setRobots] = useState<Robot[]>([]);
  const [missions, setMissions] = useState<TransportMission[]>([]);
  const [carryStatus, setCarryStatus] = useState<CarryStatusResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const fetchRobots = useCallback(async () => {
    try {
      setError(null);

      const [carryData, missionsData] = await Promise.all([
        getCarryRobotsStatus().catch(() => null),
        getMissions({ limit: 50 }).catch(() => []),
      ]);

      if (carryData) {
        setCarryStatus(carryData);
      }
      setMissions(missionsData);

      const allRobots: Robot[] = [];

      if (carryData?.robots) {
        allRobots.push(...carryData.robots.map(carryRobotToFrontend));
      }

      setRobots(allRobots);
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to fetch robots';
      setError(message);
      console.error('Failed to fetch robots:', err);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchRobots();

    if (pollInterval > 0) {
      pollRef.current = setInterval(fetchRobots, pollInterval);
    }

    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current);
      }
    };
  }, [fetchRobots, pollInterval]);

  const cancelTask = useCallback(async (robotId: string) => {
    try {
      setError(null);

      const activeMission = missions.find(
        m => m.carryRobotId === robotId &&
        ['pending', 'en_route', 'arrived'].includes(m.status)
      );

      if (activeMission) {

        try {
          await apiCancelDeliveryMission(activeMission.missionId, 'web');
        } catch {

          await apiCancelMission(activeMission.missionId);
        }
        await fetchRobots();
      } else {

        setRobots(prev => prev.map(robot =>
          robot.id === robotId
            ? { ...robot, status: 'Idle', destination: '', taskDescription: undefined }
            : robot
        ));
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to cancel task';
      setError(message);
      console.error('Failed to cancel task:', err);
      throw err;
    }
  }, [missions, fetchRobots]);

  const refresh = useCallback(() => {
    setLoading(true);
    fetchRobots();
  }, [fetchRobots]);

  return {
    robots,
    missions,
    carryStatus,
    loading,
    error,
    cancelTask,
    refresh,
  };
}
