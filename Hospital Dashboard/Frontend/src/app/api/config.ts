// API Configuration
// When using Vite proxy, we don't need the full URL - just use relative paths
export const API_BASE_URL = import.meta.env.VITE_API_URL || '';

export const API_ENDPOINTS = {
  // Patients
  patients: '/api/patients',
  patientsBeds: '/api/patients/beds',
  patientsOccupancy: '/api/patients/occupancy',
  
  // Users
  users: '/api/users',
  
  // Events  
  events: '/api/events',
  eventsButton: '/api/events/button',
  eventsStatsDaily: '/api/events/stats/daily',
  eventsStatsDailyByUser: '/api/events/stats/daily-by-user',
  
  // Robots
  robots: '/api/robots',
  robotsCarryStatus: '/api/robots/carry/status',
  robotsBipedActive: '/api/robots/biped/active',
  
  // Maps
  maps: '/api/maps',
  
  // Missions
  missions: '/api/missions',
  missionsTransport: '/api/missions/transport',
  missionsDelivery: '/api/missions/delivery',
  missionsCarryNext: '/api/missions/carry/next',
  missionsCarryCancel: '/api/missions/carry',
  missionsDeliveryHistory: '/api/missions/delivery/history',
  
  // Biped Sessions
  bipedSessions: '/api/robots/biped/sessions',
  bipedSessionHistory: '/api/robots/biped/sessions/history',
  
  // Alerts
  alerts: '/api/alerts',
  
  // Uploads
  uploads: '/uploads',
};

// Helper function to build full URL (without using new URL for relative paths)
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