import express from 'express';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';
import { publishCommand, publishCarryStackJson, STACK_ROBOT_ID } from '../services/mqttService.js';
import { ROUTE_TEST_MED_TO_R4M3 } from '../utils/checkpointIds.js';
import { LOW_BATTERY_PCT, ROBOT_ONLINE_TIMEOUT_MS } from '../utils/constants.js';

const router = express.Router();

// ---- SSE live-stream registry ----
const sseClients = new Set();

/** Push a robot-position event to all connected SSE clients */
export function emitRobotPosition(data) {
  if (sseClients.size === 0) return;
  const msg = `data: ${JSON.stringify(data)}\n\n`;
  for (const res of sseClients) {
    try { res.write(msg); } catch { sseClients.delete(res); }
  }
}

/** SSE endpoint: GET /api/robots/live */
router.get('/live', (req, res) => {
  res.setHeader('Content-Type',  'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection',    'keep-alive');
  res.setHeader('X-Accel-Buffering', 'no'); // nginx pass-through
  res.flushHeaders();

  // Send a heartbeat every 15 s so the browser doesn't time out
  const hb = setInterval(() => {
    try { res.write(': heartbeat\n\n'); } catch { clearInterval(hb); }
  }, 15000);

  sseClients.add(res);

  req.on('close', () => {
    sseClients.delete(res);
    clearInterval(hb);
  });
});

function cleanString(v, max = 64) {
  return String(v ?? '').trim().slice(0, max);
}

function normalizeType(t) {
  const v = cleanString(t, 16).toLowerCase();
  if (v === 'carry') return v;
  return null;
}

function normalizeStatus(s) {
  const v = cleanString(s, 32).toLowerCase();
  const allowed = ['idle', 'busy', 'charging', 'maintenance', 'offline', 'low_battery', 'follow', 'manual', 'waiting'];
  return allowed.includes(v) ? v : null;
}

router.put('/:id/telemetry', async (req, res) => {
  try {
    const robotId = String(req.params.id || '').trim();
    if (!robotId) return res.status(400).send('robotId required');

    const body = req.body || {};
    const now = new Date();

    console.log(`[Telemetry] Received from ${robotId}:`, JSON.stringify(body));

    const type = normalizeType(body.type) || 'carry';
    let status = normalizeStatus(body.status) || 'idle';
    const telemetryNodeId = cleanString(
      body.currentNodeId || body.currentLocation?.room || '',
      32
    ).toUpperCase();

    const batteryLevel =
      (typeof body.batteryLevel === 'number' && Number.isFinite(body.batteryLevel))
        ? Math.max(0, Math.min(100, body.batteryLevel))
        : 100;

    if (type === 'carry') {
      let activeMission = await TransportMission.findOne({
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

      if (!activeMission) status = 'idle';
      else status = 'busy';
    }

    if (batteryLevel < LOW_BATTERY_PCT) status = 'low_battery';

    const update = {
      robotId,
      name: cleanString(body.name || `Carry ${robotId}`, 64),
      type,
      status,
      batteryLevel,
      firmwareVersion: cleanString(body.firmwareVersion || '', 32) || undefined,
      lastSeenAt: now,
    };

    if (body.currentLocation) update.currentLocation = body.currentLocation;

    if (status === 'idle') update.transportData = {};

    await Robot.updateOne({ robotId }, { $set: update }, { upsert: true });
    res.json({ ok: true, status });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.get('/carry/status', async (_req, res) => {
  try {
    const now = Date.now();

    const all = await Robot.find({ type: 'carry' }).lean();
    const online = all.filter(r => r.lastSeenAt && (now - new Date(r.lastSeenAt).getTime() <= ROBOT_ONLINE_TIMEOUT_MS));

    const robotIds = online.map(r => r.robotId);
    const activeMissions = await TransportMission.find({
      carryRobotId: { $in: robotIds },
      returnedAt: null,
      status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] }
    }).lean();
    const missionMap = {};
    for (const m of activeMissions) {
      missionMap[m.carryRobotId] = m;
    }

    const robots = online.map(r => {
      const mission = missionMap[r.robotId];

      const location = r.currentLocation?.room ||
        (typeof r.currentLocation === 'string' ? r.currentLocation : '—');

      const destination = mission
        ? (mission.bedId || mission.destinationNodeId || '—')
        : (r.transportData?.destination?.room || '—');

      const currentNode = mission?.currentNodeId || '';

      return {
        robotId: r.robotId,
        name: r.name,
        status: r.status,
        statusText: r.status,
        batteryLevel: r.batteryLevel ?? 0,
        carrying: r.transportData?.carryingItem || '—',
        destination,
        location,
        currentNode,
        robotMode: r.status === 'follow' ? 'follow' : 'auto',
      };
    });

    const summary = {
      total: robots.length,
      idle: robots.filter(x => x.status === 'idle').length,
      busy: robots.filter(x => x.status === 'busy').length,
      charging: robots.filter(x => x.status === 'charging').length,
    };

    res.json({ summary, robots });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

/**
 * POST /api/robots/:id/command
 * Gửi lệnh MQTT tới robot.
 * Body: { command: "set_mode", mode: "follow" | "auto" }
 * Chỉ cho phép set_mode: follow/idle — gate MED nằm trong firmware.
 */
router.post('/:id/command', async (req, res) => {
  try {
    const robotId = String(req.params.id || '').trim();
    if (!robotId) return res.status(400).json({ error: 'robotId required' });

    const { command, mode, ...rest } = req.body || {};
    if (!command) return res.status(400).json({ error: 'command required' });

    const STACK_COMMANDS = [
      'stack_route_test',
      'stack_route_now',
      'stack_start',
      'stack_cancel',
      'stack_status',
      'relay_set',
      'relay_resume',
    ];
    if (STACK_COMMANDS.includes(command)) {
      if (robotId !== STACK_ROBOT_ID) {
        return res.status(400).json({
          error: `Lệnh stack chỉ dùng với robotId "${STACK_ROBOT_ID}" (MQTT_STACK_ROBOT_ID)`,
        });
      }
      let sent = false;
      if (command === 'stack_route_test') {
        sent = publishCarryStackJson({ action: 'route', ids: ROUTE_TEST_MED_TO_R4M3 });
      } else if (command === 'stack_route_now') {
        sent = publishCarryStackJson({
          action: 'route',
          ids: ROUTE_TEST_MED_TO_R4M3,
          immediate: true,
        });
      } else if (command === 'stack_start') {
        sent = publishCarryStackJson({ action: 'start' });
      } else if (command === 'stack_cancel') {
        sent = publishCarryStackJson({ action: 'cancel' });
      } else if (command === 'stack_status') {
        sent = publishCarryStackJson({ action: 'status' });
      } else if (command === 'relay_set') {
        const which = String(rest.which || '').trim();
        const on =
          rest.on === true ||
          rest.on === 1 ||
          String(rest.on).toLowerCase() === 'true';
        if (!['vision', 'line', 'nfc'].includes(which)) {
          return res.status(400).json({
            error: 'relay_set cần which: "vision" | "line" | "nfc" và on: boolean',
          });
        }
        sent = publishCarryStackJson({ action: 'relay', which, on });
      } else if (command === 'relay_resume') {
        sent = publishCarryStackJson({ action: 'relay_resume' });
      }
      if (!sent) {
        return res.status(503).json({ error: 'MQTT broker not connected' });
      }
      if (command.startsWith('stack_')) {
        console.log(`[CMD] ${command} → carry stack`, ROUTE_TEST_MED_TO_R4M3);
      } else {
        console.log(`[CMD] ${command} → carry stack`, rest);
      }
      return res.json({
        ok: true,
        robotId,
        command,
        ...(command.startsWith('stack_route') ? { routeIds: ROUTE_TEST_MED_TO_R4M3 } : {}),
        ...(command === 'relay_set'
          ? {
              which: String(rest.which || '').trim(),
              on:
                rest.on === true ||
                rest.on === 1 ||
                String(rest.on).toLowerCase() === 'true',
            }
          : {}),
      });
    }

    const ALLOWED_COMMANDS = [
      'set_mode',
      'goto_mode',
      'follow',
      'idle',
      'stop',
      'resume',
      'tune_turn',
      'test_dashboard',
    ];
    if (!ALLOWED_COMMANDS.includes(command)) {
      return res.status(400).json({ error: `Unknown command: ${command}` });
    }

    const params = {};
    if (mode) params.mode = mode;
    Object.assign(params, rest);

    // For carry-stack robot, route ALL commands through carry/robot/cmd
    if (robotId === STACK_ROBOT_ID) {
      const stackPayload = { action: command };
      if (mode) stackPayload.mode = mode;
      Object.assign(stackPayload, rest);
      const sent = publishCarryStackJson(stackPayload);
      if (!sent) {
        return res.status(503).json({ error: 'MQTT broker not connected' });
      }
      console.log(`[CMD] ${command} → carry stack (bridged)`, stackPayload);
      return res.json({ ok: true, robotId, command, ...(mode ? { mode } : {}) });
    }

    const sent = publishCommand(robotId, command, params);
    if (!sent) {
      return res.status(503).json({ error: 'MQTT broker not connected' });
    }

    console.log(`[CMD] ${command} → ${robotId}`, params);
    res.json({ ok: true, robotId, command, ...(mode ? { mode } : {}) });
  } catch (e) {
    res.status(500).json({ error: e?.message || 'Server error' });
  }
});

export default router;
