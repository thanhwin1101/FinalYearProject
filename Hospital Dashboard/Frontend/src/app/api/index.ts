// Re-export all API modules
export * from './config';
export * from './http';

// API Services
export * as patientsApi from './patients';
export * as robotsApi from './robots';
export * as missionsApi from './missions';
export * as mapsApi from './maps';
export * as alertsApi from './alerts';
export * as usersApi from './users';
export * as eventsApi from './events';

// Also export individual functions for convenience
export {
  getPatients,
  getPatientById,
  getPatientByMrn,
  getPatientByCard,
  createPatient,
  createPatientWithPhoto,
  updatePatient,
  updatePatientWithPhoto,
  deletePatient,
  assignPatientToBed,
  getBeds,
  getOccupancy,
  dischargePatient,
} from './patients';

export {
  getCarryRobotsStatus,
  getBipedRobotsStatus,
  updateRobotTelemetry,
  getAllRobots,
} from './robots';

export {
  getMissions,
  getMissionById,
  createMission,
  getMissionPath,
  updateMissionStatus,
  cancelMission,
  markMissionArrived,
  markMissionCompleted,
  getActiveMissionForRobot,
} from './missions';

export {
  getMapById,
  getAllMaps,
  upsertMap,
  calculateRoute,
  getNearestNode,
  addNode,
  addEdge,
  getBedsFromMap,
  getStationsFromMap,
} from './maps';

export {
  getAlerts,
  getActiveAlerts,
  createAlert,
  resolveAlert,
  resolveAlerts,
} from './alerts';

export {
  getUsers,
  createUser,
  checkUidExists,
  getUserByUid,
  deleteUser,
} from './users';

export {
  createButtonEvent,
  getDailyStats,
  getDailyStatsByUser,
} from './events';
