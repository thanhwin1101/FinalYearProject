import express from 'express';
import crypto from 'crypto';

import MapGraph from '../models/MapGraph.js';
import Robot from '../models/Robot.js';
import TransportMission from '../models/TransportMission.js';
import Alert from '../models/Alert.js';

// MQTT Service for robot communication
import { publishMissionAssign, publishMissionCancel } from '../services/mqttService.js';

const router = express.Router();

function makeId(prefix) {
  return `${prefix}-${crypto.randomBytes(6).toString('hex').toUpperCase()}`;
}

function buildAdj(map) {
  const nodes = new Map((map.nodes || []).map(n => [n.nodeId, n]));
  const adj = new Map();
  for (const k of nodes.keys()) adj.set(k, []);
  for (const e of (map.edges || [])) {
    if (!adj.has(e.from)) adj.set(e.from, []);
    if (!adj.has(e.to)) adj.set(e.to, []);
    const w = (typeof e.weight === 'number') ? e.weight : 1;
    adj.get(e.from).push({ to: e.to, w });
    adj.get(e.to).push({ to: e.from, w });
  }
  return { nodes, adj };
}

function pickStartNodeId(map) {
  const nodes = map.nodes || [];
  if (nodes.some(n => n.nodeId === 'MED')) return 'MED';
  const medRoom = nodes.find(n => n.kind === 'room' && /med/i.test(n.label || ''));
  if (medRoom) return medRoom.nodeId;
  const station = nodes.find(n => n.kind === 'station');
  if (station) return station.nodeId;
  return nodes[0]?.nodeId || null;
}

function normalizeBedId(bedId) {
  const s = String(bedId || '').trim();
  let m = /^R(\d+)([MO])(\d+)$/i.exec(s);
  if (m) {
    const room = Number(m[1]);
    const side = m[2].toUpperCase();
    const idx = Number(m[3]);
    if (room >= 1 && room <= 4 && idx >= 1 && idx <= 3) return `R${room}${side}${idx}`;
    return s.toUpperCase();
  }
  m = /^R(\d+)-Bed(\d+)$/i.exec(s);
  if (m) {
    const room = Number(m[1]);
    const bed = Number(m[2]);
    if (!(room >= 1 && room <= 4) || !(bed >= 1 && bed <= 6)) return null;
    // Bed1->M1, Bed2->O1, Bed3->M2, Bed4->O2, Bed5->M3, Bed6->O3
    const idx = Math.ceil(bed / 2);
    const side = (bed % 2 === 1) ? 'M' : 'O';
    return `R${room}${side}${idx}`;
  }
  return s.toUpperCase();
}

function parseBedId(normalBedId) {
  const m = /^R(\d)([MO])(\d)$/.exec(String(normalBedId || ''));
  if (!m) return null;
  const room = Number(m[1]);
  const side = m[2];
  const idx = Number(m[3]);
  if (room < 1 || room > 4 || idx < 1 || idx > 3) return null;
  return { room, side, idx };
}

function pickDestinationNodeId(map, bedId) {
  const nodes = map.nodes || [];
  const bedNode = nodes.find(n => n.kind === 'bed' && n.bedId === bedId);
  if (bedNode) return bedNode.nodeId;
  if (nodes.some(n => n.nodeId === bedId)) return bedId;
  return null;
}

/**
 * HARD ROUTES đúng theo bản đồ bạn chốt:
 * - Trục chính: MED -> H_MED -> H_BOT -> (J4 -> H_TOP nếu room 1/2)
 * - Vào phòng qua D1:
 *   + Nhánh M: D1 -> M1 -> M2 -> M3
 *   + Nhánh O: D1 -> D2 -> O1 -> O2 -> O3
 * - Chiều đi: MED->H_MED (F), tại H_MED rẽ trái để lên H_BOT, rồi mới tiếp tục.
 * - Chiều về:
 *   + O: O1->D2->(rẽ)->D1->(thẳng)->hub
 *   + M: M1->D1->(rẽ)->hub
 *   + Tại hub: room 1/3 rẽ phải để đi xuống; room 2/4 rẽ trái để đi xuống.
 */
function roomProfile(room) {
  const isLeftSideRoom = (room === 1 || room === 3);
  const hubNode = (room <= 2) ? 'H_TOP' : 'H_BOT';

  return {
    room,
    isLeftSideRoom,
    hubNode,

    corridorOut: (room <= 2)
      ? ['MED', 'H_MED', 'H_BOT', 'J4', 'H_TOP']
      : ['MED', 'H_MED', 'H_BOT'],

    corridorBack: (room <= 2)
      ? ['H_TOP', 'J4', 'H_BOT', 'H_MED', 'MED']
      : ['H_BOT', 'H_MED', 'MED'],

    // Vào phòng từ hub -> D1
    enterRoomTurn: isLeftSideRoom ? 'L' : 'R',

    // Trong phòng (outbound)
    // - M: tại D1 rẽ xuống M1 (room 1/3: L, room 2/4: R)
    toMFromD1Turn: isLeftSideRoom ? 'L' : 'R',

    // - O: D1->D2 (thẳng), tại D2 rẽ xuống O1 (room 1/3: L, room 2/4: R)
    toOFromD2Turn: isLeftSideRoom ? 'L' : 'R',

    // Trong phòng (return)
    // - O: tại D2 rẽ để về D1 (room 1/3: R, room 2/4: L)
    backToD1FromD2Turn: isLeftSideRoom ? 'R' : 'L',

    // - M: tại D1 rẽ để ra hub (room 1/3: R, room 2/4: L)
    exitRoomTurnAtD1_M: isLeftSideRoom ? 'R' : 'L',

    // Tại hub khi bắt đầu đi xuống trục chính (return):
    // room 1/3 rẽ phải; room 2/4 rẽ trái
    exitHubTurnToBack: isLeftSideRoom ? 'R' : 'L',
  };
}

function buildHardRouteNodeIds(normalBedId) {
  const parsed = parseBedId(normalBedId);
  if (!parsed) return null;

  const { room, side, idx } = parsed;
  const prof = roomProfile(room);

  const D1 = `R${room}D1`;
  const D2 = `R${room}D2`;

  const bedsSeq = [];
  for (let k = 1; k <= idx; k++) bedsSeq.push(`R${room}${side}${k}`);

  // OUTBOUND
  const outNodeIds = [...prof.corridorOut, D1];
  if (side === 'M') {
    // M: D1 -> M1 -> M2 -> ...
    outNodeIds.push(...bedsSeq);
  } else {
    // O: D1 -> D2 -> O1 -> O2 -> ...
    outNodeIds.push(D2, ...bedsSeq);
  }

  // RETURN
  const backNodeIds = [];
  backNodeIds.push(normalBedId);

  for (let k = idx - 1; k >= 1; k--) backNodeIds.push(`R${room}${side}${k}`);

  if (side === 'M') {
    // M: ... -> M1 -> D1
    backNodeIds.push(D1);
  } else {
    // O: ... -> O1 -> D2 -> D1
    backNodeIds.push(D2, D1);
  }

  backNodeIds.push(...prof.corridorBack);

  return { parsed, prof, outNodeIds, backNodeIds };
}

// Actions stored on the checkpoint that is READ, to orient robot towards NEXT checkpoint.
function actionsForLeg(from, to, ctx, phase) {
  const { room, side, prof } = ctx;
  const D1 = `R${room}D1`;
  const D2 = `R${room}D2`;

  const isBed = new RegExp(`^R${room}[MO][1-3]$`);

  // ===== OUTBOUND =====
  if (phase === 'out') {
    // Trục chính (theo yêu cầu): MED thẳng ra H_MED, rồi tại H_MED rẽ trái để lên H_BOT
    if (from === 'MED' && to === 'H_MED') return ['F'];
    if (from === 'H_MED' && to === 'H_BOT') return ['L', 'F'];

    // Room 1/2: H_BOT -> J4 -> H_TOP
    if (from === 'H_BOT' && to === 'J4') return ['F'];
    if (from === 'J4' && to === 'H_TOP') return ['F'];

    // Vào phòng: hub -> D1
    if (from === prof.hubNode && to === D1) return [prof.enterRoomTurn, 'F'];

    // Trong phòng
    if (side === 'M') {
      // D1 -> M1 (rẽ xuống nhánh M)
      if (from === D1 && to === `R${room}M1`) return [prof.toMFromD1Turn, 'F'];
      // M chain
      if (isBed.test(from) && isBed.test(to)) return ['F'];
    } else {
      // D1 -> D2 (thẳng)
      if (from === D1 && to === D2) return ['F'];
      // D2 -> O1 (rẽ xuống nhánh O)
      if (from === D2 && to === `R${room}O1`) return [prof.toOFromD2Turn, 'F'];
      // O chain
      if (isBed.test(from) && isBed.test(to)) return ['F'];
    }

    return ['F'];
  }

  // ===== RETURN =====

  // 1) Trục chính (từ hub về MED)
  if (room <= 2) {
    // H_TOP -> J4: cần rẽ tại H_TOP theo side room (1/3: R, 2/4: L)
    if (from === 'H_TOP' && to === 'J4') return [prof.exitHubTurnToBack, 'F'];
    if (from === 'J4' && to === 'H_BOT') return ['F'];
    // Đoạn này từ J4 xuống H_BOT rồi xuống H_MED là thẳng
    if (from === 'H_BOT' && to === 'H_MED') return ['F'];
  } else {
    // Room 3/4: ngay tại H_BOT đi xuống H_MED cần rẽ theo room (3: R, 4: L)
    if (from === 'H_BOT' && to === 'H_MED') return [prof.exitHubTurnToBack, 'F'];
  }

  // H_MED -> MED: rẽ phải để vào MED (đúng logic: outbound H_MED rẽ trái để đi lên)
  if (from === 'H_MED' && to === 'MED') return ['R', 'F'];

  // 2) Trong phòng (từ giường ra D1)
  if (side === 'M') {
    // M chain
    if (isBed.test(from) && isBed.test(to)) return ['F'];
    // M1 -> D1 (thẳng đi lên)
    if (from === `R${room}M1` && to === D1) return ['F'];
    // D1 -> hub (rẽ để ra cửa theo room)
    if (from === D1 && to === prof.corridorBack[0]) return [prof.exitRoomTurnAtD1_M, 'F'];
  } else {
    // O chain
    if (isBed.test(from) && isBed.test(to)) return ['F'];
    // O1 -> D2 (thẳng đi lên)
    if (from === `R${room}O1` && to === D2) return ['F'];
    // D2 -> D1 (rẽ để về cửa)
    if (from === D2 && to === D1) return [prof.backToD1FromD2Turn, 'F'];
    // D1 -> hub (thẳng ra hub)
    if (from === D1 && to === prof.corridorBack[0]) return ['F'];
  }

  return ['F'];
}

function computeActionsMap(nodeIds, ctx, phase) {
  const m = new Map();
  for (let i = 0; i < nodeIds.length - 1; i++) {
    const from = nodeIds[i];
    const to = nodeIds[i + 1];
    const actions = actionsForLeg(from, to, ctx, phase);

    const hasTurn = actions.includes('L') || actions.includes('R');
    const normalized = hasTurn
      ? (actions.includes('F') ? actions : [...actions, 'F'])
      : (actions.length ? actions : ['F']);

    m.set(from, normalized);
  }
  m.set(nodeIds[nodeIds.length - 1], []);
  return m;
}

function legacyActionFromActions(actions = []) {
  const t = actions.find(x => x === 'L' || x === 'R');
  return t || (actions[0] || 'F');
}

function toPoints(graph, map, nodeIds, actionsMap) {
  return nodeIds.map(nodeId => {
    const n = graph.nodes.get(nodeId);
    if (!n || !n.coordinates) throw new Error(`Map node missing coordinates: ${nodeId}`);

    const actions = Array.isArray(actionsMap?.get(nodeId)) ? actionsMap.get(nodeId) : [];
    const action = legacyActionFromActions(actions);

    return {
      nodeId,
      x: n.coordinates.x,
      y: n.coordinates.y,
      floor: map.floor,
      building: map.building,
      rfidUid: n.rfidUid || null,
      kind: n.kind || '',
      label: n.label || '',
      action,
      actions
    };
  });
}

function mustExist(graph, ids) {
  return ids.every(id => graph.nodes.has(id));
}

// ================== API ==================

// GET /missions/transport - List transport missions
router.get('/transport', async (req, res) => {
  try {
    const { status, robotId, limit = 50 } = req.query;
    
    const query = {};
    
    // Filter by status (can be comma-separated like "pending,en_route,arrived")
    if (status) {
      const statuses = String(status).split(',').map(s => s.trim());
      query.status = { $in: statuses };
    }
    
    // Filter by robotId
    if (robotId) {
      query.carryRobotId = String(robotId).trim();
    }
    
    const missions = await TransportMission
      .find(query)
      .sort({ createdAt: -1 })
      .limit(Number(limit))
      .lean();
    
    res.json(missions);
  } catch (e) {
    console.error('List missions error:', e);
    res.status(500).send(e?.message || 'Server error');
  }
});

router.post('/delivery', async (req, res) => {
  try {
    const { mapId, bedId, patientName, requestedByUid } = req.body || {};
    if (!mapId || !bedId) return res.status(400).send('mapId/bedId required');
    if (!patientName) return res.status(400).send('patientName required');

    const map = await MapGraph.findOne({ mapId }).lean();
    if (!map) return res.status(404).send('Map not found');

    const onlineSince = new Date(Date.now() - 30_000);  // 30 seconds online check
    const carry = await Robot.findOne({
      type: 'carry',
      status: 'idle',
      lastSeenAt: { $gte: onlineSince }
    }).lean();

    if (!carry) return res.status(409).send('No idle ONLINE carry robot available');

    const normalBedId = normalizeBedId(bedId);
    if (!normalBedId) return res.status(400).send('Invalid bedId');

    const startNodeId = pickStartNodeId(map);
    const destNodeId = pickDestinationNodeId(map, normalBedId);
    if (!startNodeId || !destNodeId) return res.status(404).send('Start/destination node not found');

    const graph = buildAdj(map);

    const hard = buildHardRouteNodeIds(normalBedId);
    if (!hard) return res.status(400).send('Unsupported bedId (hard-route only)');

    const { outNodeIds, backNodeIds } = hard;

    if (!mustExist(graph, outNodeIds) || !mustExist(graph, backNodeIds)) {
      const missingOut = outNodeIds.filter(id => !graph.nodes.has(id));
      const missingBack = backNodeIds.filter(id => !graph.nodes.has(id));
      return res.status(422).json({
        message: 'Map is missing required nodeIds for hard-route',
        missingOut,
        missingBack
      });
    }

    const ctx = { room: hard.parsed.room, side: hard.parsed.side, idx: hard.parsed.idx, prof: hard.prof };
    const outActionsMap = computeActionsMap(outNodeIds, ctx, 'out');
    const backActionsMap = computeActionsMap(backNodeIds, ctx, 'return');

    const outboundRoute = toPoints(graph, map, outNodeIds, outActionsMap);
    const returnRoute = toPoints(graph, map, backNodeIds, backActionsMap);

    const missionId = makeId('TM');

    await TransportMission.create({
      missionId,
      mapId,
      carryRobotId: carry.robotId,
      requestedByUid: requestedByUid || undefined,
      patientName,
      bedId: normalBedId,
      destinationNodeId: destNodeId,

      outboundRoute,
      returnRoute,
      plannedRoute: outboundRoute,

      status: 'pending',
      assignedAt: new Date(),
      returnedAt: null
    });

    await Robot.updateOne(
      { robotId: carry.robotId },
      {
        $set: {
          status: 'busy',
          'transportData.destination.building': map.building,
          'transportData.destination.floor': map.floor,
          'transportData.destination.room': normalBedId,
          'transportData.carryingItem': 'medical item'
        }
      }
    );

    // Publish mission to robot via MQTT
    publishMissionAssign(carry.robotId, {
      missionId,
      status: 'pending',
      patientName,
      bedId: normalBedId,
      outboundRoute,
      returnRoute
    });

    res.json({
      ok: true,
      missionId,
      carryRobotId: carry.robotId,
      outCount: outboundRoute.length,
      backCount: returnRoute.length
    });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.get('/carry/next', async (req, res) => {
  try {
    const robotId = String(req.query.robotId || '').trim();
    if (!robotId) return res.status(400).send('robotId required');

    const m = await TransportMission.findOne({
      carryRobotId: robotId,
      status: { $in: ['pending', 'en_route', 'arrived', 'completed', 'cancelled'] },
      returnedAt: null
    }).sort({ requestedAt: -1 }).lean();

    if (!m) return res.json({ mission: null });

    const outRaw = (m.outboundRoute && m.outboundRoute.length) ? m.outboundRoute : (m.plannedRoute || []);
    const backRaw = (m.returnRoute && m.returnRoute.length) ? m.returnRoute : (outRaw.slice().reverse());

    const normalizePoint = (p) => {
      const legacy = (p.action === 'L' || p.action === 'R') ? p.action : 'F';
      const actions = (Array.isArray(p.actions) && p.actions.length)
        ? p.actions.filter(x => x === 'F' || x === 'L' || x === 'R')
        : (legacy === 'L' || legacy === 'R') ? [legacy, 'F'] : ['F'];

      return {
        nodeId: p.nodeId,
        x: p.x,
        y: p.y,
        rfidUid: p.rfidUid || null,
        kind: p.kind || '',
        label: p.label || '',
        action: legacy,
        actions
      };
    };

    res.json({
      mission: {
        missionId: m.missionId,
        mapId: m.mapId,
        bedId: m.bedId,
        patientName: m.patientName || '',
        status: m.status,

        outboundRoute: outRaw.map(normalizePoint),
        returnRoute: backRaw.map(normalizePoint),
        plannedRoute: (m.plannedRoute || []).map(normalizePoint)
      }
    });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.put('/carry/:missionId/progress', async (req, res) => {
  try {
    const missionId = req.params.missionId;
    const { status, currentNodeId, batteryLevel, note } = req.body || {};

    const m = await TransportMission.findOne({ missionId });
    if (!m) return res.status(404).send('Mission not found');

    if (status && ['pending', 'en_route', 'arrived', 'completed', 'failed', 'cancelled'].includes(status)) {
      m.status = status;
      if (status === 'en_route' && !m.startedAt) m.startedAt = new Date();
      if (status === 'arrived' && !m.arrivedAt) m.arrivedAt = new Date();
      if (status === 'cancelled' && !m.cancelledAt) m.cancelledAt = new Date();
    }

    if (currentNodeId) {
      const routeAll = [
        ...(m.outboundRoute || []),
        ...(m.returnRoute || []),
        ...(m.plannedRoute || [])
      ];
      const p = routeAll.find(x => x.nodeId === currentNodeId);
      m.actualRoute.push({
        x: p?.x ?? 0,
        y: p?.y ?? 0,
        floor: p?.floor,
        building: p?.building,
        timestamp: new Date()
      });
    }

    if (note) m.notes.push(String(note).slice(0, 200));
    await m.save();

    if (typeof batteryLevel === 'number') {
      await Robot.updateOne({ robotId: m.carryRobotId }, { $set: { batteryLevel } });

      if (batteryLevel <= 20 && !m.lowBatteryAlerted) {
        m.lowBatteryAlerted = true;
        await m.save();

        await Alert.create({
          type: 'carry_low_battery',
          level: 'high',
          robotId: m.carryRobotId,
          missionId: m.missionId,
          message: `Carry robot low battery (${batteryLevel}%) while delivering to ${m.bedId}`,
          data: { batteryLevel, bedId: m.bedId }
        });

        await Robot.updateOne({ robotId: m.carryRobotId }, { $set: { status: 'low_battery' } });
      }
    }

    res.json({ ok: true });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.post('/carry/:missionId/complete', async (req, res) => {
  try {
    const missionId = req.params.missionId;
    const { result, note } = req.body || {};

    const m = await TransportMission.findOne({ missionId });
    if (!m) return res.status(404).send('Mission not found');

    m.status = (result === 'failed') ? 'failed' : 'completed';
    m.completedAt = new Date();
    if (note) m.notes.push(String(note).slice(0, 200));
    await m.save();

    await Robot.updateOne(
      { robotId: m.carryRobotId },
      {
        $set: {
          status: 'busy',
          'transportData.destination.room': 'MED',
          'transportData.carryingItem': '—'
        }
      }
    );

    res.json({ ok: true });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.post('/carry/:missionId/cancel', async (req, res) => {
  try {
    const missionId = req.params.missionId;
    const { cancelledBy, note } = req.body || {};

    const m = await TransportMission.findOne({ missionId });
    if (!m) return res.status(404).send('Mission not found');

    if (m.returnedAt) return res.status(409).send('Mission already returned');

    m.status = 'cancelled';
    m.cancelRequestedAt = m.cancelRequestedAt || new Date();
    m.cancelledAt = new Date();
    m.cancelledBy = cancelledBy || 'web';
    if (note) m.notes.push(String(note).slice(0, 200));
    await m.save();

    await Robot.updateOne(
      { robotId: m.carryRobotId },
      {
        $set: {
          status: 'busy',
          'transportData.destination.room': 'MED',
          'transportData.carryingItem': '—'
        }
      }
    );

    // Publish cancel to robot via MQTT
    publishMissionCancel(m.carryRobotId, missionId);

    res.json({ ok: true });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

router.post('/carry/:missionId/returned', async (req, res) => {
  try {
    const missionId = req.params.missionId;
    const { note } = req.body || {};

    const m = await TransportMission.findOne({ missionId });
    if (!m) return res.status(404).send('Mission not found');

    if (!m.returnedAt) m.returnedAt = new Date();
    if (note) m.notes.push(String(note).slice(0, 200));
    await m.save();

    await Robot.updateOne(
      { robotId: m.carryRobotId },
      { $set: { status: 'idle', transportData: {} }, $inc: { totalDeliveries: (m.status === 'completed') ? 1 : 0 } }
    );

    res.json({ ok: true });
  } catch (e) {
    res.status(500).send(e?.message || 'Server error');
  }
});

// GET /missions/delivery/history - Get delivery history for Carry robots
router.get('/delivery/history', async (req, res) => {
  try {
    const { robotId, patientId, startDate, endDate, status, page = 1, limit = 50 } = req.query;
    
    const query = {};
    
    if (robotId) query.carryRobotId = robotId;
    if (patientId) query.patientId = patientId;
    if (status) query.status = status;
    
    // Date range filter
    if (startDate || endDate) {
      query.createdAt = {};
      if (startDate) query.createdAt.$gte = new Date(startDate);
      if (endDate) query.createdAt.$lte = new Date(endDate);
    }
    
    const skip = (parseInt(page) - 1) * parseInt(limit);
    
    const [missions, total] = await Promise.all([
      TransportMission.find(query)
        .sort({ createdAt: -1 })
        .skip(skip)
        .limit(parseInt(limit))
        .lean(),
      TransportMission.countDocuments(query)
    ]);
    
    // Transform missions to history format
    const history = missions.map(m => {
      const startTime = m.startedAt || m.createdAt;
      const endTime = m.completedAt || m.returnedAt || m.cancelledAt;
      const duration = endTime ? Math.round((new Date(endTime) - new Date(startTime)) / 60000) : null;
      
      return {
        _id: m._id.toString(),
        missionId: m.missionId,
        robotId: m.carryRobotId,
        robotName: m.carryRobotId, // Can be enhanced with robot name lookup
        patientId: m.patientId,
        patientName: m.patientName || 'Unknown',
        destinationRoom: m.destinationRoomId,
        destinationBed: m.destinationBed,
        status: m.status,
        createdAt: m.createdAt,
        startedAt: m.startedAt,
        completedAt: m.completedAt,
        cancelledAt: m.cancelledAt,
        returnedAt: m.returnedAt,
        duration: duration,
        itemCarried: m.itemDescription,
        notes: m.notes
      };
    });
    
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
    console.error('Delivery history error:', e);
    res.status(500).send(e?.message || 'Server error');
  }
});

export default router;
