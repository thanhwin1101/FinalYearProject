export const API_BASE_URL = import.meta.env.VITE_API_URL || '';

export const API_ENDPOINTS = {

  patients: '/api/patients',
  patientsBeds: '/api/patients/beds',
  patientsOccupancy: '/api/patients/occupancy',

  users: '/api/users',

  events: '/api/events',
  eventsButton: '/api/events/button',
  eventsStatsDaily: '/api/events/stats/daily',
  eventsStatsDailyByUser: '/api/events/stats/daily-by-user',

  robots: '/api/robots',
  robotsCarryStatus: '/api/robots/carry/status',
  robotsLiveStream: '/api/robots/live',
  robotsCommand: '/api/robots',  // dùng: /api/robots/:id/command


  maps: '/api/maps',

  missions: '/api/missions',
  missionsTransport: '/api/missions/transport',
  missionsDelivery: '/api/missions/delivery',
  missionsCarryNext: '/api/missions/carry/next',
  missionsCarryCancel: '/api/missions/carry',
  missionsDeliveryHistory: '/api/missions/delivery/history',



  alerts: '/api/alerts',

  uploads: '/uploads',
};

export function buildUrl(endpoint: string, params?: Record<string, string | number | boolean>): string {
  let url = API_BASE_URL ? `${API_BASE_URL}${endpoint}` : endpoint;

  if (params) {
    const searchParams = new URLSearchParams();
    Object.entries(params).forEach(([key, value]) => {
      if (value !== undefined && value !== null && value !== '') {
        searchParams.append(key, String(value));
      }
    });
    const queryString = searchParams.toString();
    if (queryString) {
      url += (url.includes('?') ? '&' : '?') + queryString;
    }
  }

  return url;
}
