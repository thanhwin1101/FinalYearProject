import { API_ENDPOINTS } from './config';
import { get, post, put } from './http';

// Transport Mission interfaces
export interface TransportMission {
  _id: string;
  missionId: string;
  carryRobotId?: string;
  bedId: string;
  patientId?: string;
  item?: string;
  status: 'pending' | 'en_route' | 'arrived' | 'completed' | 'cancelled';
  path?: string[];
  createdAt: string;
  startedAt?: string;
  arrivedAt?: string;
  returnedAt?: string;
  cancelledAt?: string;
}

export interface CreateMissionData {
  bedId: string;
  patientId?: string;
  item?: string;
  carryRobotId?: string;
}

export interface MissionPathResponse {
  missionId: string;
  commands: Array<{
    action: 'forward' | 'turn' | 'stop';
    value?: number | string;
  }>;
  path: string[];
}

// Get all transport missions
export async function getMissions(params?: {
  status?: string;
  robotId?: string;
  limit?: number;
}): Promise<TransportMission[]> {
  return get<TransportMission[]>(API_ENDPOINTS.missionsTransport, { params });
}

// Get mission by ID
export async function getMissionById(id: string): Promise<TransportMission> {
  return get<TransportMission>(`${API_ENDPOINTS.missionsTransport}/${id}`);
}

// Create new transport mission
export async function createMission(data: CreateMissionData): Promise<TransportMission> {
  return post<TransportMission>(API_ENDPOINTS.missionsTransport, data);
}

// Get mission path/commands for robot
export async function getMissionPath(missionId: string): Promise<MissionPathResponse> {
  return get<MissionPathResponse>(`${API_ENDPOINTS.missionsTransport}/${missionId}/path`);
}

// Update mission status
export async function updateMissionStatus(
  missionId: string, 
  status: 'arrived' | 'completed' | 'cancelled'
): Promise<TransportMission> {
  return put<TransportMission>(`${API_ENDPOINTS.missionsTransport}/${missionId}/status`, { status });
}

// Cancel mission
export async function cancelMission(missionId: string): Promise<TransportMission> {
  return put<TransportMission>(`${API_ENDPOINTS.missionsTransport}/${missionId}/cancel`, {});
}

// Mark mission as arrived
export async function markMissionArrived(missionId: string): Promise<TransportMission> {
  return put<TransportMission>(`${API_ENDPOINTS.missionsTransport}/${missionId}/arrived`, {});
}

// Mark mission as completed (returned)
export async function markMissionCompleted(missionId: string): Promise<TransportMission> {
  return put<TransportMission>(`${API_ENDPOINTS.missionsTransport}/${missionId}/returned`, {});
}

// Get active mission for a robot
export async function getActiveMissionForRobot(robotId: string): Promise<TransportMission | null> {
  const missions = await getMissions({ robotId, status: 'pending,en_route,arrived' });
  return missions.length > 0 ? missions[0] : null;
}

// ============================================================
// Delivery Mission API (for Carry Robot)
// ============================================================

export interface DeliveryMissionRequest {
  mapId: string;
  bedId: string;
  patientName: string;
  requestedByUid?: string;
}

export interface DeliveryMissionResponse {
  ok: boolean;
  missionId: string;
  carryRobotId: string;
  outCount: number;
  backCount: number;
}

export interface CarryMissionDetails {
  missionId: string;
  mapId: string;
  bedId: string;
  patientName: string;
  status: string;
  outboundRoute: Array<{
    nodeId: string;
    x: number;
    y: number;
    rfidUid: string | null;
    action: string;
    actions: string[];
  }>;
  returnRoute: Array<{
    nodeId: string;
    x: number;
    y: number;
    rfidUid: string | null;
    action: string;
    actions: string[];
  }>;
}

// Create delivery mission for Carry Robot
export async function createDeliveryMission(data: DeliveryMissionRequest): Promise<DeliveryMissionResponse> {
  return post<DeliveryMissionResponse>(API_ENDPOINTS.missionsDelivery, data);
}

// Cancel delivery mission
export async function cancelDeliveryMission(missionId: string, cancelledBy?: string): Promise<{ ok: boolean }> {
  return post<{ ok: boolean }>(`${API_ENDPOINTS.missionsCarryCancel}/${missionId}/cancel`, {
    cancelledBy: cancelledBy || 'web'
  });
}

// Get next mission for robot (used by robot firmware)
export async function getCarryNextMission(robotId: string): Promise<{ mission: CarryMissionDetails | null }> {
  return get<{ mission: CarryMissionDetails | null }>(`${API_ENDPOINTS.missionsCarryNext}?robotId=${robotId}`);
}
// ============================================================
// Delivery History API
// ============================================================

export interface DeliveryHistoryItem {
  _id: string;
  missionId: string;
  robotId: string;
  robotName: string;
  patientId: string;
  patientName: string;
  destinationRoom: string;
  destinationBed: string;
  status: 'pending' | 'en_route' | 'arrived' | 'completed' | 'cancelled';
  createdAt: string;
  startedAt?: string;
  completedAt?: string;
  cancelledAt?: string;
  returnedAt?: string;
  duration?: number; // in minutes
  itemCarried?: string;
  notes?: string[];
}

export interface DeliveryHistoryResponse {
  history: DeliveryHistoryItem[];
  pagination: {
    page: number;
    limit: number;
    total: number;
    totalPages: number;
  };
}

// Get delivery history
export async function getDeliveryHistory(params?: {
  limit?: number;
  page?: number;
  robotId?: string;
  patientId?: string;
  status?: string;
  startDate?: string;
  endDate?: string;
}): Promise<DeliveryHistoryResponse> {
  const queryParams = new URLSearchParams();
  if (params?.limit) queryParams.append('limit', params.limit.toString());
  if (params?.page) queryParams.append('page', params.page.toString());
  if (params?.robotId) queryParams.append('robotId', params.robotId);
  if (params?.patientId) queryParams.append('patientId', params.patientId);
  if (params?.status) queryParams.append('status', params.status);
  if (params?.startDate) queryParams.append('startDate', params.startDate);
  if (params?.endDate) queryParams.append('endDate', params.endDate);
  
  const url = queryParams.toString()
    ? `${API_ENDPOINTS.missionsDeliveryHistory}?${queryParams.toString()}`
    : API_ENDPOINTS.missionsDeliveryHistory;
  
  return get<DeliveryHistoryResponse>(url);
}