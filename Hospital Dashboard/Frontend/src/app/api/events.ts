import { API_ENDPOINTS } from './config';
import { get, post } from './http';

export interface Event {
  _id: string;
  uid: string;
  at: string;
  deviceTs?: number;
}

export interface DailyStat {
  _id: string;
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

export async function createButtonEvent(data: ButtonEventData): Promise<{ ok: boolean; id: string }> {
  return post<{ ok: boolean; id: string }>(API_ENDPOINTS.eventsButton, data);
}

export async function getDailyStats(params?: {
  uid?: string;
  from?: string;
  to?: string;
}): Promise<DailyStat[]> {
  return get<DailyStat[]>(API_ENDPOINTS.eventsStatsDaily, { params });
}

export async function getDailyStatsByUser(params?: {
  from?: string;
  to?: string;
}): Promise<DailyByUserStat[]> {
  return get<DailyByUserStat[]>(API_ENDPOINTS.eventsStatsDailyByUser, { params });
}
