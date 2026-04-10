import { useState, useEffect, useCallback, useRef } from 'react';
import {
  getCarryRobotsStatus,
  CarryRobotStatus,
} from '@/app/api/robots';
import { Robot, RobotStatus } from '@/app/types/robot';

function mapStatus(backendStatus: string): RobotStatus {
  const statusMap: Record<string, RobotStatus> = {
    'idle': 'Idle',
    'busy': 'Moving',
    'follow': 'Moving',
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
    robotMode: robot.robotMode ?? (robot.status === 'follow' ? 'follow' : 'auto'),
  };
}

export function useRobots(pollInterval: number = 5000) {
  const [robots, setRobots] = useState<Robot[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const fetchRobots = useCallback(async () => {
    try {
      setError(null);
      const carryData = await getCarryRobotsStatus().catch(() => null);
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

  const refresh = useCallback(() => {
    setLoading(true);
    fetchRobots();
  }, [fetchRobots]);

  return { robots, loading, error, refresh };
}
