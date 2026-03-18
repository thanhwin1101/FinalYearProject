import express from 'express';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';

const router = express.Router();

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

    if (batteryLevel <= 20) status = 'low_battery';

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
    const ONLINE_MS = 15000;
    const now = Date.now();

    const all = await Robot.find({ type: 'carry' }).lean();
    const online = all.filter(r => r.lastSeenAt && (now - new Date(r.lastSeenAt).getTime() <= ONLINE_MS));

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

export default router;
