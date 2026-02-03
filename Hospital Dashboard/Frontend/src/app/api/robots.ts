import { API_ENDPOINTS } from './config';
import { get, put } from './http';

// Backend Robot interfaces
export interface BackendRobot {
  _id?: string;
  robotId: string;
  name: string;
  type: 'carry' | 'biped';
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

export interface BipedStatusResponse {
  summary: {
    totalActiveRobots: number;
    totalStepsToday: number;
    lowBatteryRobots: number;
  };
  robots: BackendRobot[];
}

export interface TelemetryData {
  name?: string;
  type?: 'carry' | 'biped';
  status?: string;
  batteryLevel?: number;
  currentLocation?: {
    x?: number;
    y?: number;
    room?: string;
  };
  firmwareVersion?: string;
}

// Get carry robots status
export async function getCarryRobotsStatus(): Promise<CarryStatusResponse> {
  return get<CarryStatusResponse>(API_ENDPOINTS.robotsCarryStatus);
}

// Get biped robots status
export async function getBipedRobotsStatus(): Promise<BipedStatusResponse> {
  return get<BipedStatusResponse>(API_ENDPOINTS.robotsBipedActive);
}

// Update robot telemetry (used by ESP32)
export async function updateRobotTelemetry(robotId: string, data: TelemetryData): Promise<{ ok: boolean; status: string }> {
  return put<{ ok: boolean; status: string }>(`${API_ENDPOINTS.robots}/${robotId}/telemetry`, data);
}

// Get all robots (combined)
export async function getAllRobots(): Promise<{ carry: CarryStatusResponse; biped: BipedStatusResponse }> {
  const [carry, biped] = await Promise.all([
    getCarryRobotsStatus(),
    getBipedRobotsStatus()
  ]);
  
  return { carry, biped };
}

// Biped session history interfaces
export interface BipedSessionHistoryItem {
  _id: string;
  sessionId: string;
  robotId: string;
  robotName: string;
  userId: string;
  userName: string;
  patientId?: string;
  patientName?: string;
  startTime: string;
  endTime?: string;
  totalSteps: number;
  duration: number; // in minutes
  status: 'active' | 'completed' | 'interrupted';
}

export interface BipedSessionHistoryResponse {
  history: BipedSessionHistoryItem[];
  pagination: {
    page: number;
    limit: number;
    total: number;
    totalPages: number;
  };
}

// Get biped session history
export async function getBipedSessionHistory(params?: {
  robotId?: string;
  userId?: string;
  startDate?: string;
  endDate?: string;
  page?: number;
  limit?: number;
}): Promise<BipedSessionHistoryResponse> {
  const queryParams = new URLSearchParams();
  if (params?.robotId) queryParams.append('robotId', params.robotId);
  if (params?.userId) queryParams.append('userId', params.userId);
  if (params?.startDate) queryParams.append('startDate', params.startDate);
  if (params?.endDate) queryParams.append('endDate', params.endDate);
  if (params?.page) queryParams.append('page', params.page.toString());
  if (params?.limit) queryParams.append('limit', params.limit.toString());
  
  const url = queryParams.toString() 
    ? `${API_ENDPOINTS.bipedSessionHistory}?${queryParams.toString()}`
    : API_ENDPOINTS.bipedSessionHistory;
  
  return get<BipedSessionHistoryResponse>(url);
}
