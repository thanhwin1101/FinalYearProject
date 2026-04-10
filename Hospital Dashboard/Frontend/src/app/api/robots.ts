import { API_ENDPOINTS } from './config';
import { get, post, put } from './http';

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
  robotMode?: 'follow' | 'auto';
}

export interface RobotCommandResponse {
  ok: boolean;
  robotId?: string;
  command?: string;
  mode?: string;
  error?: string;
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

/**
 * Gửi lệnh MQTT tới robot qua backend.
 * command: 'set_mode' | 'stop' | 'resume'
 * mode (khi set_mode): 'follow' | 'idle'
 * Lưu ý: firmware yêu cầu quét MED checkpoint trước khi chuyển mode.
 */
export async function sendRobotCommand(
  robotId: string,
  command: string,
  mode?: string
): Promise<RobotCommandResponse> {
  const body: Record<string, string> = { command };
  if (mode) body.mode = mode;
  return post<RobotCommandResponse>(`${API_ENDPOINTS.robots}/${robotId}/command`, body);
}

/** Gửi lệnh MQTT với body đầy đủ (tune_turn, test_dashboard, v.v.) */
export async function sendRobotCommandPayload(
  robotId: string,
  body: Record<string, unknown>
): Promise<RobotCommandResponse> {
  return post<RobotCommandResponse>(`${API_ENDPOINTS.robots}/${robotId}/command`, body);
}
