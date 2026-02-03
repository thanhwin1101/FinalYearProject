import { API_ENDPOINTS } from './config';
import { get, post } from './http';

export interface Event {
  _id: string;
  uid: string;
  at: string;
  deviceTs?: number;
}

export interface DailyStat {
  _id: string;  // date string YYYY-MM-DD
  count: number;
}

export interface DailyByUserStat {
  _id: {
    day: string;
    uid: string;
  };
  count: number;
}

export interface ButtonEventData {
  uid: string;
  timestamp?: number;
}

// Create button event (from ESP32)
export async function createButtonEvent(data: ButtonEventData): Promise<{ ok: boolean; id: string }> {
  return post<{ ok: boolean; id: string }>(API_ENDPOINTS.eventsButton, data);
}

// Get daily stats
export async function getDailyStats(params?: {
  uid?: string;
  from?: string;
  to?: string;
}): Promise<DailyStat[]> {
  return get<DailyStat[]>(API_ENDPOINTS.eventsStatsDaily, { params });
}

// Get daily stats by user (for stacked charts)
export async function getDailyStatsByUser(params?: {
  from?: string;
  to?: string;
}): Promise<DailyByUserStat[]> {
  return get<DailyByUserStat[]>(API_ENDPOINTS.eventsStatsDailyByUser, { params });
}
