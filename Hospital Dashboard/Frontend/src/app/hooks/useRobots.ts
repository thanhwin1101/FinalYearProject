import { useState, useEffect, useCallback, useRef } from 'react';
import { 
  getCarryRobotsStatus, 
  getBipedRobotsStatus,
  CarryStatusResponse,
  BipedStatusResponse,
  CarryRobotStatus,
  BackendRobot
} from '@/app/api/robots';
import { 
  getMissions, 
  cancelMission as apiCancelMission,
  cancelDeliveryMission as apiCancelDeliveryMission,
  TransportMission 
} from '@/app/api/missions';
import { Robot, RobotStatus } from '@/app/types/robot';

// Map backend status to frontend status
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

// Convert backend carry robot to frontend format
function carryRobotToFrontend(robot: CarryRobotStatus): Robot {
  return {
    id: robot.robotId,
    type: 'Carry',
    name: robot.name || `Carry-${robot.robotId}`,
    currentLocation: robot.location || 'Unknown',
    destination: robot.destination || '',
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
  const [bipedStatus, setBipedStatus] = useState<BipedStatusResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  // Fetch robot data
  const fetchRobots = useCallback(async () => {
    try {
      setError(null);
      
      const [carryData, bipedData, missionsData] = await Promise.all([
        getCarryRobotsStatus().catch(() => null),
        getBipedRobotsStatus().catch(() => null),
        getMissions({ limit: 50 }).catch(() => []),
      ]);

      if (carryData) {
        setCarryStatus(carryData);
      }
      if (bipedData) {
        setBipedStatus(bipedData);
      }
      setMissions(missionsData);

      // Combine robots
      const allRobots: Robot[] = [];
      
      // Add carry robots
      if (carryData?.robots) {
        allRobots.push(...carryData.robots.map(carryRobotToFrontend));
      }

      // Add biped robots (if any)
      if (bipedData?.robots) {
        bipedData.robots.forEach((robot: BackendRobot) => {
          allRobots.push({
            id: robot.robotId,
            type: 'Biped',
            name: robot.name || `Biped-${robot.robotId}`,
            currentLocation: robot.currentLocation?.room || 'Unknown',
            destination: '',
            status: mapStatus(robot.status),
            batteryLevel: robot.batteryLevel || 0,
            lastUpdated: robot.lastSeenAt || new Date().toISOString(),
          });
        });
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

  // Initial fetch and polling
  useEffect(() => {
    fetchRobots();

    // Set up polling
    if (pollInterval > 0) {
      pollRef.current = setInterval(fetchRobots, pollInterval);
    }

    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current);
      }
    };
  }, [fetchRobots, pollInterval]);

  // Cancel robot task/mission
  const cancelTask = useCallback(async (robotId: string) => {
    try {
      setError(null);
      
      // Find active mission for this robot
      const activeMission = missions.find(
        m => m.carryRobotId === robotId && 
        ['pending', 'en_route', 'arrived'].includes(m.status)
      );

      if (activeMission) {
        // Try the new delivery mission cancel API first
        try {
          await apiCancelDeliveryMission(activeMission.missionId, 'web');
        } catch {
          // Fallback to old cancel API
          await apiCancelMission(activeMission.missionId);
        }
        await fetchRobots(); // Refresh data
      } else {
        // Update local state
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

  // Manual refresh
  const refresh = useCallback(() => {
    setLoading(true);
    fetchRobots();
  }, [fetchRobots]);

  return {
    robots,
    missions,
    carryStatus,
    bipedStatus,
    loading,
    error,
    cancelTask,
    refresh,
  };
}
