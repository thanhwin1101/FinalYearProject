/**
 * MQTT Service for Hospital Dashboard Backend
 * Handles communication with Carry Robots via MQTT
 */

import mqtt from 'mqtt';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';
import MapGraph from '../models/MapGraph.js';
import Alert from '../models/Alert.js';

// MQTT Configuration - có thể override qua environment variables
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://localhost:1883';
const MQTT_USER = process.env.MQTT_USER || 'hospital_backend';
const MQTT_PASS = process.env.MQTT_PASS || '123456';
const MQTT_CLIENT_ID = `hospital-backend-${Date.now()}`;

// Topic templates
const TOPICS = {
  TELEMETRY: 'hospital/robots/+/telemetry',
  MISSION_PROGRESS: 'hospital/robots/+/mission/progress',
  MISSION_COMPLETE: 'hospital/robots/+/mission/complete',
  MISSION_RETURNED: 'hospital/robots/+/mission/returned',
  POSITION_REPORT: 'hospital/robots/+/position/waiting_return',  // NEW: Robot báo vị trí khi cancel
};

let client = null;
let connected = false;

/**
 * Initialize MQTT connection
 */
export function initMqtt() {
  console.log(`[MQTT] Connecting to ${MQTT_BROKER}...`);
  
  client = mqtt.connect(MQTT_BROKER, {
    clientId: MQTT_CLIENT_ID,
    username: MQTT_USER,
    password: MQTT_PASS,
    clean: true,
    reconnectPeriod: 5000,
    connectTimeout: 10000,
  });

  client.on('connect', () => {
    connected = true;
    console.log('[MQTT] Connected to broker');
    
    // Subscribe to robot topics
    Object.values(TOPICS).forEach(topic => {
      client.subscribe(topic, (err) => {
        if (err) {
          console.error(`[MQTT] Subscribe error for ${topic}:`, err.message);
        } else {
          console.log(`[MQTT] Subscribed to ${topic}`);
        }
      });
    });
  });

  client.on('disconnect', () => {
    connected = false;
    console.log('[MQTT] Disconnected');
  });

  client.on('error', (err) => {
    console.error('[MQTT] Connection error:', err.message);
  });

  client.on('message', handleMessage);

  return client;
}

/**
 * Handle incoming MQTT messages
 */
async function handleMessage(topic, message) {
  try {
    const payload = JSON.parse(message.toString());
    
    // Extract robotId from topic: hospital/robots/{robotId}/...
    const topicParts = topic.split('/');
    const robotId = topicParts[2];
    
    console.log(`[MQTT] Received from ${robotId}:`, topic);

    // Route to appropriate handler
    if (topic.includes('/telemetry')) {
      await handleTelemetry(robotId, payload);
    } else if (topic.includes('/mission/progress')) {
      await handleProgress(robotId, payload);
    } else if (topic.includes('/mission/complete')) {
      await handleComplete(robotId, payload);
    } else if (topic.includes('/mission/returned')) {
      await handleReturned(robotId, payload);
    } else if (topic.includes('/position/waiting_return')) {
      await handleWaitingReturnRoute(robotId, payload);  // NEW handler
    }
  } catch (err) {
    console.error('[MQTT] Message parse error:', err.message);
  }
}

/**
 * Handle robot telemetry (mirrors PUT /:id/telemetry logic)
 */
async function handleTelemetry(robotId, payload) {
  try {
    const now = new Date();
    const type = payload.type || 'carry';
    let status = payload.status || 'idle';

    const batteryLevel =
      (typeof payload.batteryLevel === 'number' && Number.isFinite(payload.batteryLevel))
        ? Math.max(0, Math.min(100, payload.batteryLevel))
        : 100;

    // Mission-aware status for carry robots
    if (type === 'carry') {
      const active = await TransportMission.exists({
        carryRobotId: robotId,
        returnedAt: null,
        status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] }
      });
      status = active ? 'busy' : 'idle';
    }

    // Low battery takes priority
    if (batteryLevel <= 20) status = 'low_battery';

    const update = {
      robotId,
      name: payload.name || `Carry-${robotId.replace(/^CARRY-/i, '')}`,
      type,
      status,
      batteryLevel,
      lastSeenAt: now,
    };

    if (payload.firmwareVersion) update.firmwareVersion = payload.firmwareVersion;
    
    // Set currentLocation from payload
    if (payload.currentLocation) {
      update.currentLocation = payload.currentLocation;
    } else if (payload.currentNodeId) {
      update['currentLocation.room'] = payload.currentNodeId;
    }

    // Set destination from payload or active mission
    if (status !== 'idle' && type === 'carry') {
      if (payload.destBed) {
        update['transportData.destination.room'] = payload.destBed;
      } else {
        const mission = await TransportMission.findOne({
          carryRobotId: robotId,
          returnedAt: null,
          status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] }
        }).lean();
        if (mission?.bedId) {
          update['transportData.destination.room'] = mission.bedId;
        }
      }
    }

    // Clear transportData when idle
    if (status === 'idle') update.transportData = {};

    await Robot.findOneAndUpdate(
      { robotId },
      { $set: update },
      { upsert: true, new: true }
    );

    console.log(`[MQTT] Telemetry updated for ${robotId}: status=${status}, battery=${batteryLevel}%`);
  } catch (err) {
    console.error('[MQTT] Telemetry update error:', err.message);
  }
}

/**
 * Handle mission progress update
 */
async function handleProgress(robotId, payload) {
  try {
    const { missionId, status, currentNodeId, batteryLevel, note } = payload;
    if (!missionId) return;

    const update = {
      updatedAt: new Date(),
    };
    
    if (status) update.status = status;
    if (currentNodeId) update.currentNodeId = currentNodeId;
    if (note) {
      update.$push = { notes: { timestamp: new Date(), text: note } };
    }

    await TransportMission.findOneAndUpdate(
      { missionId },
      update
    );

    // Update robot position & destination
    if (currentNodeId) {
      const robotUpdate = { 
        'currentLocation.room': currentNodeId,
        lastSeenAt: new Date(),
      };
      if (typeof batteryLevel === 'number') robotUpdate.batteryLevel = batteryLevel;
      
      // Also set destination from mission data
      if (missionId) {
        const missionDoc = await TransportMission.findOne({ missionId }).lean();
        if (missionDoc?.bedId) {
          robotUpdate['transportData.destination.room'] = missionDoc.bedId;
        }
      }

      await Robot.findOneAndUpdate(
        { robotId },
        { $set: robotUpdate }
      );
    }
  } catch (err) {
    console.error('[MQTT] Progress update error:', err.message);
  }
}

/**
 * Handle mission complete
 */
async function handleComplete(robotId, payload) {
  try {
    const { missionId, result, note } = payload;
    if (!missionId) return;

    await TransportMission.findOneAndUpdate(
      { missionId },
      {
        $set: {
          status: 'completed',
          completedAt: new Date(),
          result: result || 'ok',
          updatedAt: new Date(),
        },
        $push: note ? { notes: { timestamp: new Date(), text: note } } : undefined,
      }
    );

    // Update robot status
    await Robot.findOneAndUpdate(
      { robotId },
      { $set: { status: 'idle', currentMissionId: null, lastSeen: new Date() } }
    );

    console.log(`[MQTT] Mission ${missionId} completed by ${robotId}`);
  } catch (err) {
    console.error('[MQTT] Complete handler error:', err.message);
  }
}

/**
 * Handle robot returned to base
 */
async function handleReturned(robotId, payload) {
  try {
    const { missionId, note } = payload;
    if (!missionId) return;

    await TransportMission.findOneAndUpdate(
      { missionId },
      {
        $set: {
          returnedAt: new Date(),
          updatedAt: new Date(),
        },
        $push: note ? { notes: { timestamp: new Date(), text: note } } : undefined,
      }
    );

    // Update robot status
    await Robot.findOneAndUpdate(
      { robotId },
      { 
        $set: { 
          status: 'idle', 
          currentMissionId: null,
          currentLocation: 'MED',
          lastSeen: new Date()
        } 
      }
    );

    console.log(`[MQTT] Robot ${robotId} returned for mission ${missionId}`);
  } catch (err) {
    console.error('[MQTT] Returned handler error:', err.message);
  }
}

// ========================================
// RETURN ROUTE CALCULATION (NEW)
// ========================================

/**
 * Handle robot reporting position and waiting for return route
 * Robot gửi: { missionId, currentNodeId }
 * Backend tính route từ currentNodeId về MED và gửi lại
 */
async function handleWaitingReturnRoute(robotId, payload) {
  try {
    const { missionId, currentNodeId } = payload;
    if (!missionId || !currentNodeId) {
      console.error('[MQTT] Missing missionId or currentNodeId in waiting_return');
      return;
    }

    console.log(`[MQTT] Robot ${robotId} waiting at ${currentNodeId} for return route`);

    // Get mission to find mapId
    const mission = await TransportMission.findOne({ missionId }).lean();
    if (!mission) {
      console.error(`[MQTT] Mission ${missionId} not found`);
      return;
    }

    // Calculate return route from current position
    const returnRoute = await calculateReturnRouteFromNode(mission.mapId, currentNodeId);
    
    if (!returnRoute || returnRoute.length < 2) {
      console.error(`[MQTT] Could not calculate return route from ${currentNodeId}`);
      // Send error to robot
      publishReturnRoute(robotId, missionId, [], 'error');
      return;
    }

    // Update mission with new status
    await TransportMission.findOneAndUpdate(
      { missionId },
      { 
        $set: { 
          status: 'returning',
          currentNodeId,
          returnRoute,
          updatedAt: new Date()
        },
        $push: { notes: { timestamp: new Date(), text: `Return route calculated from ${currentNodeId}` } }
      }
    );

    // Send return route to robot
    publishReturnRoute(robotId, missionId, returnRoute, 'ok');
    console.log(`[MQTT] Return route sent to ${robotId}: ${returnRoute.length} nodes`);

  } catch (err) {
    console.error('[MQTT] handleWaitingReturnRoute error:', err.message);
  }
}

/**
 * Calculate return route from any node back to MED
 * Uses the corridor path based on node position
 */
async function calculateReturnRouteFromNode(mapId, fromNodeId) {
  try {
    const map = await MapGraph.findOne({ mapId }).lean();
    if (!map) return null;

    const nodes = new Map((map.nodes || []).map(n => [n.nodeId, n]));
    
    // Parse the current node to determine which corridor to use
    const nodeIds = buildReturnPath(fromNodeId);
    if (!nodeIds || nodeIds.length < 2) return null;

    // Convert to route points
    const routePoints = nodeIds.map(nodeId => {
      const n = nodes.get(nodeId);
      return {
        nodeId,
        x: n?.coordinates?.x || 0,
        y: n?.coordinates?.y || 0,
        floor: map.floor,
        building: map.building,
        rfidUid: n?.rfidUid || null,
        kind: n?.kind || '',
        label: n?.label || '',
        action: 'F',  // Actions sẽ được tính riêng
        actions: ['F']
      };
    });

    // Compute actions for return path
    computeReturnActions(routePoints, fromNodeId);

    return routePoints;
  } catch (err) {
    console.error('[MQTT] calculateReturnRouteFromNode error:', err.message);
    return null;
  }
}

/**
 * Build return path node IDs from any checkpoint back to MED
 */
function buildReturnPath(fromNodeId) {
  // Main corridor nodes
  const corridorTop = ['H_TOP', 'J4', 'H_BOT', 'H_MED', 'MED'];  // Room 1/2
  const corridorBot = ['H_BOT', 'H_MED', 'MED'];  // Room 3/4
  
  // If already on main corridor
  if (fromNodeId === 'MED') return ['MED'];
  if (fromNodeId === 'H_MED') return ['H_MED', 'MED'];
  if (fromNodeId === 'H_BOT') return ['H_BOT', 'H_MED', 'MED'];
  if (fromNodeId === 'J4') return ['J4', 'H_BOT', 'H_MED', 'MED'];
  if (fromNodeId === 'H_TOP') return corridorTop;
  
  // Parse room/bed nodeId (e.g., R1M2, R2D1, etc.)
  const roomMatch = /^R(\d)([MDO])(\d)?$/.exec(fromNodeId);
  if (!roomMatch) {
    console.error(`[MQTT] Unknown node format: ${fromNodeId}`);
    return null;
  }

  const room = parseInt(roomMatch[1]);
  const type = roomMatch[2];  // M, O, or D
  const idx = roomMatch[3] ? parseInt(roomMatch[3]) : 1;
  
  const D1 = `R${room}D1`;
  const D2 = `R${room}D2`;
  const hubNode = (room <= 2) ? 'H_TOP' : 'H_BOT';
  const corridor = (room <= 2) ? corridorTop : corridorBot;
  
  const path = [fromNodeId];
  
  if (type === 'M') {
    // M beds: Mx -> M(x-1) -> ... -> M1 -> D1 -> hub -> corridor
    for (let i = idx - 1; i >= 1; i--) {
      path.push(`R${room}M${i}`);
    }
    path.push(D1);
  } else if (type === 'O') {
    // O beds: Ox -> O(x-1) -> ... -> O1 -> D2 -> D1 -> hub -> corridor
    for (let i = idx - 1; i >= 1; i--) {
      path.push(`R${room}O${i}`);
    }
    path.push(D2, D1);
  } else if (type === 'D') {
    // Door nodes
    if (idx === 2) {
      path.push(D1);  // D2 -> D1 -> hub
    }
    // D1 goes straight to hub
  }
  
  // Add corridor path from hub to MED
  path.push(...corridor);
  
  return path;
}

/**
 * Compute turn actions for return path
 */
function computeReturnActions(routePoints, startNode) {
  if (!routePoints || routePoints.length < 2) return;

  const roomMatch = /^R(\d)/.exec(startNode);
  const room = roomMatch ? parseInt(roomMatch[1]) : 0;
  const isLeftSideRoom = (room === 1 || room === 3);

  for (let i = 0; i < routePoints.length - 1; i++) {
    const from = routePoints[i].nodeId;
    const to = routePoints[i + 1].nodeId;
    
    let action = 'F';
    
    // Determine turns based on position
    const D1 = `R${room}D1`;
    const D2 = `R${room}D2`;
    const hubNode = (room <= 2) ? 'H_TOP' : 'H_BOT';
    
    // Inside room returns
    if (from === D2 && to === D1) {
      action = isLeftSideRoom ? 'R' : 'L';  // Exit O branch to D1
    } else if (from === D1 && to === hubNode) {
      action = isLeftSideRoom ? 'R' : 'L';  // Exit room to hub
    }
    // Hub to corridor
    else if (from === hubNode) {
      action = isLeftSideRoom ? 'R' : 'L';  // Exit hub to main corridor
    }
    // H_MED to MED (right turn)
    else if (from === 'H_MED' && to === 'MED') {
      action = 'R';
    }
    
    routePoints[i].action = action;
    routePoints[i].actions = action !== 'F' ? [action, 'F'] : ['F'];
  }
  
  // Last node has no action
  if (routePoints.length > 0) {
    routePoints[routePoints.length - 1].action = 'F';
    routePoints[routePoints.length - 1].actions = [];
  }
}

/**
 * Publish return route to robot
 */
export function publishReturnRoute(robotId, missionId, returnRoute, status = 'ok') {
  if (!connected || !client) {
    console.error('[MQTT] Not connected - cannot publish return route');
    return false;
  }

  const topic = `hospital/robots/${robotId}/mission/return_route`;
  const payload = JSON.stringify({
    missionId,
    status,  // 'ok' or 'error'
    returnRoute
  });

  client.publish(topic, payload, { qos: 1, retain: false }, (err) => {
    if (err) {
      console.error(`[MQTT] Return route publish error:`, err.message);
    } else {
      console.log(`[MQTT] Return route sent to ${robotId} (${returnRoute.length} nodes)`);
    }
  });

  return true;
}

// ========================================
// PUBLISHING FUNCTIONS (Backend -> Robot)
// ========================================

/**
 * Publish mission assignment to robot
 * @param {string} robotId - Target robot ID
 * @param {object} mission - Mission object with routes
 */
export function publishMissionAssign(robotId, mission) {
  if (!connected || !client) {
    console.error('[MQTT] Not connected - cannot publish mission');
    return false;
  }

  const topic = `hospital/robots/${robotId}/mission/assign`;
  const payload = JSON.stringify({
    mission: {
      missionId: mission.missionId,
      status: mission.status,
      patientName: mission.patientName,
      bedId: mission.bedId,
      outboundRoute: mission.outboundRoute,
      returnRoute: mission.returnRoute,
    }
  });

  client.publish(topic, payload, { qos: 1, retain: false }, (err) => {
    if (err) {
      console.error(`[MQTT] Publish error to ${topic}:`, err.message);
    } else {
      console.log(`[MQTT] Mission ${mission.missionId} assigned to ${robotId}`);
    }
  });

  return true;
}

/**
 * Publish mission cancellation to robot
 * @param {string} robotId - Target robot ID
 * @param {string} missionId - Mission to cancel
 */
export function publishMissionCancel(robotId, missionId) {
  if (!connected || !client) {
    console.error('[MQTT] Not connected - cannot publish cancel');
    return false;
  }

  const topic = `hospital/robots/${robotId}/mission/cancel`;
  const payload = JSON.stringify({ missionId });

  client.publish(topic, payload, { qos: 1, retain: false }, (err) => {
    if (err) {
      console.error(`[MQTT] Cancel publish error:`, err.message);
    } else {
      console.log(`[MQTT] Cancel sent for mission ${missionId} to ${robotId}`);
    }
  });

  return true;
}

/**
 * Publish command to robot (stop, resume, etc.)
 * @param {string} robotId - Target robot ID
 * @param {string} command - Command name
 * @param {object} params - Optional command parameters
 */
export function publishCommand(robotId, command, params = {}) {
  if (!connected || !client) {
    console.error('[MQTT] Not connected - cannot publish command');
    return false;
  }

  const topic = `hospital/robots/${robotId}/command`;
  const payload = JSON.stringify({ command, ...params });

  client.publish(topic, payload, { qos: 1, retain: false }, (err) => {
    if (err) {
      console.error(`[MQTT] Command publish error:`, err.message);
    } else {
      console.log(`[MQTT] Command '${command}' sent to ${robotId}`);
    }
  });

  return true;
}

/**
 * Check if MQTT is connected
 */
export function isConnected() {
  return connected;
}

/**
 * Get MQTT client instance
 */
export function getClient() {
  return client;
}

export default {
  initMqtt,
  publishMissionAssign,
  publishMissionCancel,
  publishReturnRoute,
  publishCommand,
  isConnected,
  getClient,
};
