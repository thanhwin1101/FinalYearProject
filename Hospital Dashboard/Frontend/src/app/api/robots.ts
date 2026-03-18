import { API_ENDPOINTS } from './config';
import { get, put } from './http';

export type BackendRobotType = 'carry';

export interface BackendRobot {
  _id?: string;
  robotId: string;
  name: string;
  type: BackendRobotType;
  status: 'idle' | 'busy' | 'charging' | 'maintenance' | 'offline' | 'low_battery';
  batteryLevel: number;
  currentLocation?: {
    x?: number;
    y?: number;
    room?: string;
  };
  firmwareVersion?: string;
  lastSeenAt?: string;
  transportData?: {
    carryingItem?: string;
    destination?: {
      room?: string;
    };
  };
}

export interface CarryRobotStatus {
  robotId: string;
  name: string;
  status: string;
  statusText: string;
  batteryLevel: number;
  carrying: string;
  destination: string;
  location: string;
  currentNode: string;
}

export interface CarryStatusResponse {
  summary: {
    total: number;
    idle: number;
    busy: number;
    charging: number;
  };
  robots: CarryRobotStatus[];
}

export interface TelemetryData {
  name?: string;
  type?: 'carry';
  status?: string;
  batteryLevel?: number;
  currentLocation?: {
    x?: number;
    y?: number;
    room?: string;
  };
  firmwareVersion?: string;
}

export async function getCarryRobotsStatus(): Promise<CarryStatusResponse> {
  return get<CarryStatusResponse>(API_ENDPOINTS.robotsCarryStatus);
}

export async function updateRobotTelemetry(robotId: string, data: TelemetryData): Promise<{ ok: boolean; status: string }> {
  return put<{ ok: boolean; status: string }>(`${API_ENDPOINTS.robots}/${robotId}/telemetry`, data);
}
