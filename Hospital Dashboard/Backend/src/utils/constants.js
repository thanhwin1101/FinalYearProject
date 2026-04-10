// Shared constants used across routes and MQTT service.
// Import from here rather than repeating magic numbers.

/** Battery percentage below which missions are rejected and status set to low_battery */
export const LOW_BATTERY_PCT = 30;

/** Robot is considered "online" if lastSeenAt is within this window (ms) */
export const ROBOT_ONLINE_TIMEOUT_MS = 30000;

/** Default map ID used when resolving routes without an explicit mission */
export const DEFAULT_MAP_ID = 'floor1';
