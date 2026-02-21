import express from 'express';
import crypto from 'crypto';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';
import BipedSession from '../models/BipedSession.js';

const router = express.Router();

function cleanString(v, max = 64) {
  return String(v ?? '').trim().slice(0, max);
}

function normalizeType(t) {
  const v = cleanString(t, 16).toLowerCase();
  if (v === 'carry' || v === 'biped') return v;
  return null;
}

function normalizeStatus(s) {
  const v = cleanString(s, 32).toLowerCase();
  const allowed = ['idle', 'busy', 'charging', 'maintenance', 'offline', 'low_battery', 'follow', 'manual', 'waiting'];
  return allowed.includes(v) ? v : null;
}

// ESP32 -> telemetry
router.put('/:id/telemetry', async (req, res) => {
  try {
    const robotId = String(req.params.id || '').trim();
    if (!robotId) return res.status(400).send('robotId required');

    const body = req.body || {};
    const now = new Date();

    console.log(`[Telemetry] Received from ${robotId}:`, JSON.stringify(body));

    const type = normalizeType(body.type) || 'carry';
    let status = normalizeStatus(body.status) || 'idle';

    const batteryLevel =
      (typeof body.batteryLevel === 'number' && Number.isFinite(body.batteryLevel))
        ? Math.max(0, Math.min(100, body.batteryLevel))
        : 100;

    // ✅ Chỉ cho busy nếu còn mission chưa returned
    if (type === 'carry') {
      const active = await TransportMission.exists({
        carryRobotId: robotId,
        returnedAt: null,
        status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] }
      });

      if (!active) status = 'idle';
      else status = 'busy';
    }

    // low battery ưu tiên
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

    // ✅ Nếu idle thì clear transportData để UI không còn "MED"
    if (status === 'idle') update.transportData = {};

    await Robot.updateOne({ robotId }, { $set: update }, { upsert: true });
    res.json({ ok: true, status });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.get('/carry/status', async (_req, res) => {
  try {
    const ONLINE_MS = 15000;  // 15 seconds - more stable detection
    const now = Date.now();

    const all = await Robot.find({ type: 'carry' }).lean();
    const online = all.filter(r => r.lastSeenAt && (now - new Date(r.lastSeenAt).getTime() <= ONLINE_MS));

    // Lookup active missions for destination info
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
      // currentLocation: prefer room string, fallback to raw string
      const location = r.currentLocation?.room || 
        (typeof r.currentLocation === 'string' ? r.currentLocation : '—');
      // destination: from active mission bed/destination node
      const destination = mission 
        ? (mission.bedId || mission.destinationNodeId || '—') 
        : (r.transportData?.destination?.room || '—');
      // Current checkpoint from mission
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

// placeholder biped
router.get('/biped/active', async (_req, res) => {
  try {
    const ONLINE_MS = 15000;  // 15 seconds - more stable detection
    const now = Date.now();

    const all = await Robot.find({ type: 'biped' }).lean();
    const online = all.filter(r => r.lastSeenAt && (now - new Date(r.lastSeenAt).getTime() <= ONLINE_MS));

    const robots = online.map(r => ({
      robotId: r.robotId,
      name: r.name,
      status: r.status,
      batteryLevel: r.batteryLevel ?? 0,
      currentUser: r.currentUser || null,
      stepCount: r.stepCount || 0,
      lastSeenAt: r.lastSeenAt
    }));

    // Get today's stats
    const todayStart = new Date();
    todayStart.setHours(0, 0, 0, 0);
    
    const todaySessions = await BipedSession.aggregate([
      { $match: { startTime: { $gte: todayStart } } },
      { $group: { _id: null, totalSteps: { $sum: '$totalSteps' } } }
    ]);

    const summary = {
      totalActiveRobots: robots.filter(r => r.status === 'busy' || r.currentUser).length,
      totalStepsToday: todaySessions[0]?.totalSteps || 0,
      lowBatteryRobots: robots.filter(r => r.batteryLevel <= 20).length
    };

    res.json({ summary, robots });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

// POST /robots/biped/session/start - Start a new biped session
router.post('/biped/session/start', async (req, res) => {
  try {
    const { robotId, userId, userName, patientId, patientName } = req.body;
    
    if (!robotId) return res.status(400).send('robotId required');
    
    // Generate session ID
    const sessionId = `BIPED-${crypto.randomBytes(6).toString('hex').toUpperCase()}`;
    
    // Get robot name
    const robot = await Robot.findOne({ robotId }).lean();
    const robotName = robot?.name || robotId;
    
    // Create new session
    const session = new BipedSession({
      sessionId,
      robotId,
      robotName,
      userId: userId || 'unknown',
      userName: userName || 'Unknown User',
      patientId,
      patientName,
      startTime: new Date(),
      status: 'active'
    });
    
    await session.save();
    
    // Update robot status
    await Robot.updateOne(
      { robotId },
      { 
        $set: { 
          status: 'busy', 
          currentUser: userName || userId,
          currentSessionId: sessionId
        } 
      }
    );
    
    res.json({ 
      ok: true, 
      sessionId,
      session: {
        sessionId: session.sessionId,
        robotId: session.robotId,
        startTime: session.startTime,
        status: session.status
      }
    });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

// PUT /robots/biped/session/:sessionId/update - Update session with steps
router.put('/biped/session/:sessionId/update', async (req, res) => {
  try {
    const { sessionId } = req.params;
    const { steps, telemetry } = req.body;
    
    const session = await BipedSession.findOne({ sessionId });
    if (!session) return res.status(404).send('Session not found');
    
    if (typeof steps === 'number') {
      session.totalSteps = steps;
    }
    
    if (telemetry) {
      session.telemetry = { ...session.telemetry, ...telemetry };
    }
    
    // Update duration
    session.duration = Math.round((Date.now() - session.startTime.getTime()) / 60000);
    
    await session.save();
    
    res.json({ ok: true });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

// POST /robots/biped/session/:sessionId/end - End a biped session
router.post('/biped/session/:sessionId/end', async (req, res) => {
  try {
    const { sessionId } = req.params;
    const { status = 'completed', note } = req.body;
    
    const session = await BipedSession.findOne({ sessionId });
    if (!session) return res.status(404).send('Session not found');
    
    session.endTime = new Date();
    session.status = status;
    session.duration = Math.round((session.endTime - session.startTime) / 60000);
    
    if (note) session.notes.push(note);
    
    await session.save();
    
    // Update robot status
    await Robot.updateOne(
      { robotId: session.robotId },
      { 
        $set: { 
          status: 'idle', 
          currentUser: null,
          currentSessionId: null
        },
        $inc: { totalSessions: 1 }
      }
    );
    
    res.json({ 
      ok: true, 
      session: {
        sessionId: session.sessionId,
        totalSteps: session.totalSteps,
        duration: session.duration,
        status: session.status
      }
    });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

// GET /robots/biped/sessions/history - Get biped session history
router.get('/biped/sessions/history', async (req, res) => {
  try {
    const { robotId, userId, patientId, startDate, endDate, status, page = 1, limit = 50 } = req.query;
    
    const query = {};
    
    if (robotId) query.robotId = robotId;
    if (userId) query.userId = userId;
    if (patientId) query.patientId = patientId;
    if (status) query.status = status;
    
    // Date range filter
    if (startDate || endDate) {
      query.startTime = {};
      if (startDate) query.startTime.$gte = new Date(startDate);
      if (endDate) query.startTime.$lte = new Date(endDate);
    }
    
    const skip = (parseInt(page) - 1) * parseInt(limit);
    
    const [sessions, total] = await Promise.all([
      BipedSession.find(query)
        .sort({ startTime: -1 })
        .skip(skip)
        .limit(parseInt(limit))
        .lean(),
      BipedSession.countDocuments(query)
    ]);
    
    const history = sessions.map(s => ({
      _id: s._id.toString(),
      sessionId: s.sessionId,
      robotId: s.robotId,
      robotName: s.robotName,
      userId: s.userId,
      userName: s.userName,
      patientId: s.patientId,
      patientName: s.patientName,
      startTime: s.startTime,
      endTime: s.endTime,
      totalSteps: s.totalSteps,
      duration: s.duration,
      status: s.status,
      telemetry: s.telemetry
    }));
    
    res.json({
      history,
      pagination: {
        page: parseInt(page),
        limit: parseInt(limit),
        total,
        totalPages: Math.ceil(total / parseInt(limit))
      }
    });
  } catch (e) {
    console.error('Biped session history error:', e);
    res.status(500).send(e?.message || 'Server error');
  }
});

export default router;
