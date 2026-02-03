import { API_ENDPOINTS } from './config';
import { get, post, put } from './http';

export type AlertLevel = 'info' | 'warning' | 'error' | 'critical';
export type AlertType = 'robot_error' | 'mission_failed' | 'low_battery' | 'connection_lost' | 'system' | 'custom';

export interface Alert {
  _id: string;
  type: AlertType;
  level: AlertLevel;
  robotId?: string;
  missionId?: string;
  message: string;
  data?: Record<string, unknown>;
  createdAt: string;
  resolvedAt?: string;
}

export interface CreateAlertData {
  type: AlertType;
  level?: AlertLevel;
  robotId?: string;
  missionId?: string;
  message: string;
  data?: Record<string, unknown>;
}

// Get alerts
export async function getAlerts(params?: {
  active?: boolean;
  limit?: number;
}): Promise<Alert[]> {
  const queryParams: Record<string, string | number | boolean> = {};
  
  if (params?.active !== undefined) {
    queryParams.active = params.active ? '1' : '0';
  }
  if (params?.limit !== undefined) {
    queryParams.limit = params.limit;
  }
  
  return get<Alert[]>(API_ENDPOINTS.alerts, { params: queryParams });
}

// Get active alerts only
export async function getActiveAlerts(limit?: number): Promise<Alert[]> {
  return getAlerts({ active: true, limit });
}

// Create new alert
export async function createAlert(data: CreateAlertData): Promise<Alert> {
  return post<Alert>(API_ENDPOINTS.alerts, data);
}

// Resolve alert
export async function resolveAlert(id: string): Promise<{ ok: boolean; alert: Alert }> {
  return put<{ ok: boolean; alert: Alert }>(`${API_ENDPOINTS.alerts}/${id}/resolve`, {});
}

// Resolve multiple alerts
export async function resolveAlerts(ids: string[]): Promise<void> {
  await Promise.all(ids.map(id => resolveAlert(id)));
}
