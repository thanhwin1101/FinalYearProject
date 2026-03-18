import mqtt from 'mqtt';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';
import MapGraph from '../models/MapGraph.js';
import Alert from '../models/Alert.js';

const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://localhost:1883';
const MQTT_USER = process.env.MQTT_USER || 'hospital_backend';
const MQTT_PASS = process.env.MQTT_PASS || '123456';
const MQTT_CLIENT_ID = `hospital-backend-${Date.now()}`;

const TOPICS = {
  TELEMETRY: 'hospital/robots/+/telemetry',
  MISSION_PROGRESS: 'hospital/robots/+/mission/progress',
  MISSION_COMPLETE: 'hospital/robots/+/mission/complete',
  MISSION_RETURNED: 'hospital/robots/+/mission/returned',
  POSITION_REPORT: 'hospital/robots/+/position/waiting_return',
};

let client = null;
let connected = false;
const legacyFieldWarned = new Set();
const MQTT_STRICT_SCHEMA = process.env.MQTT_STRICT_SCHEMA === '1' || process.env.NODE_ENV === 'staging';
const lastMissionRepublishAt = new Map();
const MISSION_REPUBLISH_COOLDOWN_MS = 5000;

function warnLegacyField(context, fieldName, replacement) {
  const key = `${context}:${fieldName}`;
  if (legacyFieldWarned.has(key)) return;
  legacyFieldWarned.add(key);
  console.warn(`[MQTT] Legacy field '${fieldName}' used in ${context}. Prefer '${replacement}'.`);
}

function normalizeNodeFields(payload = {}, context = 'unknown') {
  const usedLegacyNode = (!payload.currentNodeId && payload.nodeId);
  const usedLegacyPrev = (!payload.previousNodeId && payload.prevNodeId);

  if (usedLegacyNode) {
    if (MQTT_STRICT_SCHEMA) {
      throw new Error(`[MQTT] Rejected legacy field 'nodeId' in ${context} (strict schema enabled)`);
    }
    warnLegacyField(context, 'nodeId', 'currentNodeId');
  }
  if (usedLegacyPrev) {
    if (MQTT_STRICT_SCHEMA) {
      throw new Error(`[MQTT] Rejected legacy field 'prevNodeId' in ${context} (strict schema enabled)`);
    }
    warnLegacyField(context, 'prevNodeId', 'previousNodeId');
  }

  return {
    currentNodeId: payload.currentNodeId || null,
    previousNodeId: payload.previousNodeId || null,
  };
}

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

async function handleMessage(topic, message) {
  try {
    const payload = JSON.parse(message.toString());

    const topicParts = topic.split('/');
    const robotId = topicParts[2];

    console.log(`[MQTT] Received from ${robotId}:`, topic);

    if (topic.includes('/telemetry')) {
      await handleTelemetry(robotId, payload);
    } else if (topic.includes('/mission/progress')) {
      await handleProgress(robotId, payload);
    } else if (topic.includes('/mission/complete')) {
      await handleComplete(robotId, payload);
    } else if (topic.includes('/mission/returned')) {
      await handleReturned(robotId, payload);
    } else if (topic.includes('/position/waiting_return')) {
      await handleWaitingReturnRoute(robotId, payload);
    }
  } catch (err) {
    console.error('[MQTT] Message parse error:', err.message);
  }
}

async function handleTelemetry(robotId, payload) {
  try {
    const now = new Date();
    const type = payload.type || 'carry';
    let status = payload.status || 'idle';
    const telemetryNodeId = String(
      payload.currentNodeId || payload.currentLocation?.room || ''
    ).trim().toUpperCase();

    const batteryLevel =
      (typeof payload.batteryLevel === 'number' && Number.isFinite(payload.batteryLevel))
        ? Math.max(0, Math.min(100, payload.batteryLevel))
        : 100;

    let activeMission = null;
    if (type === 'carry') {
      activeMission = await TransportMission.findOne({
        carryRobotId: robotId,
        returnedAt: null,
        status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] }
      }).sort({ updatedAt: -1, createdAt: -1 });

      const atHomeNode = telemetryNodeId === 'MED';
      if (activeMission?.status === 'cancelled' && atHomeNode) {
        await TransportMission.updateOne(
          { missionId: activeMission.missionId, returnedAt: null },
          { $set: { returnedAt: now, updatedAt: now } }
        );
        activeMission = null;
      }

      status = activeMission ? 'busy' : 'idle';
    }

    if (type === 'carry' && activeMission) {
      const robotReportedIdle = String(payload.status || '').toLowerCase() === 'idle';
      if (robotReportedIdle) {
        const nowMs = Date.now();
        const key = `${robotId}:${activeMission.missionId}`;
        const lastMs = lastMissionRepublishAt.get(key) || 0;
        if (nowMs - lastMs >= MISSION_REPUBLISH_COOLDOWN_MS) {
          publishMissionAssign(robotId, {
            missionId: activeMission.missionId,
            status: activeMission.status,
            patientName: activeMission.patientName || '',
            bedId: activeMission.bedId || '',
            outboundRoute: activeMission.outboundRoute || [],
            returnRoute: activeMission.returnRoute || []
          });
          lastMissionRepublishAt.set(key, nowMs);
          console.log(`[MQTT] Re-published mission ${activeMission.missionId} to ${robotId} after idle telemetry`);
        }
      }
    }

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

    if (payload.currentLocation) {
      update.currentLocation = payload.currentLocation;
    } else if (payload.currentNodeId) {
      update['currentLocation.room'] = payload.currentNodeId;
    }

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

async function handleProgress(robotId, payload) {
  try {
    const { missionId, status, batteryLevel, note } = payload;
    const { currentNodeId } = normalizeNodeFields(payload, 'mission/progress');
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

    if (currentNodeId) {
      const robotUpdate = {
        'currentLocation.room': currentNodeId,
        lastSeenAt: new Date(),
      };
      if (typeof batteryLevel === 'number') robotUpdate.batteryLevel = batteryLevel;

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

    await Robot.findOneAndUpdate(
      { robotId },
      { $set: { status: 'idle', currentMissionId: null, lastSeen: new Date() } }
    );

    console.log(`[MQTT] Mission ${missionId} completed by ${robotId}`);
  } catch (err) {
    console.error('[MQTT] Complete handler error:', err.message);
  }
}

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

const CARDINAL = { N: 0, E: 1, S: 2, W: 3 };
const CARDINAL_NAME = ['N', 'E', 'S', 'W'];

function getCardinalDirection(fromCoords, toCoords) {
  const dx = (toCoords.x || 0) - (fromCoords.x || 0);
  const dy = (toCoords.y || 0) - (fromCoords.y || 0);
  if (dx === 0 && dy === 0) return CARDINAL.N;
  if (Math.abs(dx) > Math.abs(dy)) {
    return dx > 0 ? CARDINAL.E : CARDINAL.W;
  } else {
    return dy > 0 ? CARDINAL.S : CARDINAL.N;
  }
}

function getRelativeTurn(heading, required) {
  const diff = ((required - heading) + 4) % 4;
  switch (diff) {
    case 0: return 'F';
    case 1: return 'R';
    case 2: return 'B';
    case 3: return 'L';
    default: return 'F';
  }
}

async function handleWaitingReturnRoute(robotId, payload) {
  try {
    const { missionId } = payload;
    const { currentNodeId, previousNodeId } = normalizeNodeFields(payload, 'position/waiting_return');
    if (!currentNodeId) {
      console.error('[MQTT] Missing currentNodeId in waiting_return');
      return;
    }

    const effectiveMissionId = missionId || 'recovery_mode';
    const isRecovery = (effectiveMissionId === 'recovery_mode');

    console.log(`[MQTT] Robot ${robotId} waiting at ${currentNodeId} (prev: ${previousNodeId || 'none'}, mission: ${effectiveMissionId})`);

    let mapId = 'floor1';
    if (!isRecovery) {
      const mission = await TransportMission.findOne({ missionId: effectiveMissionId }).lean();
      if (mission) {
        mapId = mission.mapId;
      } else {
        console.warn(`[MQTT] Mission ${effectiveMissionId} not found in DB, using default mapId`);
      }
    }

    const returnRoute = await calculateReturnRouteFromNode(mapId, currentNodeId, previousNodeId);

    if (!returnRoute || returnRoute.length < 2) {
      console.error(`[MQTT] Could not calculate return route from ${currentNodeId}`);
      publishReturnRoute(robotId, effectiveMissionId, [], 'error');
      return;
    }

    if (!isRecovery) {
      await TransportMission.findOneAndUpdate(
        { missionId: effectiveMissionId },
        {
          $set: {
            status: 'returning',
            currentNodeId,
            returnRoute,
            updatedAt: new Date()
          },
          $push: { notes: { timestamp: new Date(), text: `Vector return route: ${previousNodeId || '?'} → ${currentNodeId} → MED (${returnRoute.length} nodes)` } }
        }
      );
    }

    publishReturnRoute(robotId, effectiveMissionId, returnRoute, 'ok');
    console.log(`[MQTT] Return route sent to ${robotId}: ${returnRoute.length} nodes, heading-aware=${!!previousNodeId}`);

  } catch (err) {
    console.error('[MQTT] handleWaitingReturnRoute error:', err.message);
  }
}

async function calculateReturnRouteFromNode(mapId, fromNodeId, previousNodeId = null) {
  try {
    const map = await MapGraph.findOne({ mapId }).lean();
    if (!map) return null;

    const nodes = new Map((map.nodes || []).map(n => [n.nodeId, n]));

    const nodeIds = buildReturnPath(fromNodeId);
    if (!nodeIds || nodeIds.length < 2) return null;

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
        action: 'F',
        actions: ['F']
      };
    });

    computeReturnActions(routePoints, fromNodeId);

    if (previousNodeId && routePoints.length >= 2) {
      const prevNode = nodes.get(previousNodeId);
      const curNode  = nodes.get(fromNodeId);

      if (prevNode?.coordinates && curNode?.coordinates) {
        const heading  = getCardinalDirection(prevNode.coordinates, curNode.coordinates);
        const required = getCardinalDirection(
          curNode.coordinates,
          { x: routePoints[1].x, y: routePoints[1].y }
        );
        const firstAction = getRelativeTurn(heading, required);

        routePoints[0].action  = firstAction;
        routePoints[0].actions = firstAction !== 'F' ? [firstAction, 'F'] : ['F'];

        console.log(
          `[MQTT] Vector2Pts: ${previousNodeId}→${fromNodeId} heading=${CARDINAL_NAME[heading]}, ` +
          `next=${routePoints[1].nodeId} required=${CARDINAL_NAME[required]}, firstAction=${firstAction}`
        );
      } else {
        console.warn(`[MQTT] Vector2Pts: Missing coordinates for ${previousNodeId} or ${fromNodeId}, using geometry-only actions`);
      }
    }

    return routePoints;
  } catch (err) {
    console.error('[MQTT] calculateReturnRouteFromNode error:', err.message);
    return null;
  }
}

function buildReturnPath(fromNodeId) {

  const corridorTop = ['H_TOP', 'J4', 'H_BOT', 'H_MED', 'MED'];
  const corridorBot = ['H_BOT', 'H_MED', 'MED'];

  if (fromNodeId === 'MED') return ['MED'];
  if (fromNodeId === 'H_MED') return ['H_MED', 'MED'];
  if (fromNodeId === 'H_BOT') return ['H_BOT', 'H_MED', 'MED'];
  if (fromNodeId === 'J4') return ['J4', 'H_BOT', 'H_MED', 'MED'];
  if (fromNodeId === 'H_TOP') return corridorTop;

  const roomMatch = /^R(\d)([MDO])(\d)?$/.exec(fromNodeId);
  if (!roomMatch) {
    console.error(`[MQTT] Unknown node format: ${fromNodeId}`);
    return null;
  }

  const room = parseInt(roomMatch[1]);
  const type = roomMatch[2];
  const idx = roomMatch[3] ? parseInt(roomMatch[3]) : 1;

  const D1 = `R${room}D1`;
  const D2 = `R${room}D2`;
  const hubNode = (room <= 2) ? 'H_TOP' : 'H_BOT';
  const corridor = (room <= 2) ? corridorTop : corridorBot;

  const path = [fromNodeId];

  if (type === 'M') {

    for (let i = idx - 1; i >= 1; i--) {
      path.push(`R${room}M${i}`);
    }
    path.push(D1);
  } else if (type === 'O') {

    for (let i = idx - 1; i >= 1; i--) {
      path.push(`R${room}O${i}`);
    }
    path.push(D2, D1);
  } else if (type === 'D') {

    if (idx === 2) {
      path.push(D1);
    }

  }

  path.push(...corridor);

  return path;
}

function computeReturnActions(routePoints, startNode) {
  if (!routePoints || routePoints.length < 2) return;

  const roomMatch = /^R(\d)/.exec(startNode);
  const room = roomMatch ? parseInt(roomMatch[1]) : 0;
  const isLeftSideRoom = (room === 1 || room === 3);

  for (let i = 0; i < routePoints.length - 1; i++) {
    const from = routePoints[i].nodeId;
    const to = routePoints[i + 1].nodeId;

    let action = 'F';

    const D1 = `R${room}D1`;
    const D2 = `R${room}D2`;
    const hubNode = (room <= 2) ? 'H_TOP' : 'H_BOT';

    if (from === D2 && to === D1) {
      action = isLeftSideRoom ? 'R' : 'L';
    } else if (from === D1 && to === hubNode) {
      action = isLeftSideRoom ? 'R' : 'L';
    }

    else if (from === hubNode) {
      action = isLeftSideRoom ? 'R' : 'L';
    }

    else if (from === 'H_MED' && to === 'MED') {
      action = 'R';
    }

    routePoints[i].action = action;
    routePoints[i].actions = action !== 'F' ? [action, 'F'] : ['F'];
  }

  if (routePoints.length > 0) {
    routePoints[routePoints.length - 1].action = 'F';
    routePoints[routePoints.length - 1].actions = [];
  }
}

export function publishReturnRoute(robotId, missionId, returnRoute, status = 'ok') {
  if (!connected || !client) {
    console.error('[MQTT] Not connected - cannot publish return route');
    return false;
  }

  const topic = `hospital/robots/${robotId}/mission/return_route`;
  const payload = JSON.stringify({
    missionId,
    status,
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

export function isConnected() {
  return connected;
}

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
