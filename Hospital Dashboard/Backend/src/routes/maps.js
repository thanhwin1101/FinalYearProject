import { Router } from 'express';
import MapGraph from '../models/MapGraph.js';

const r = Router();

function normUid(uid = '') {
  return String(uid).trim().toUpperCase();
}

function dist2(a, b) {
  const dx = (a?.coordinates?.x ?? a?.x ?? 0) - (b?.coordinates?.x ?? b?.x ?? 0);
  const dy = (a?.coordinates?.y ?? a?.y ?? 0) - (b?.coordinates?.y ?? b?.y ?? 0);
  return dx * dx + dy * dy;
}

function buildAdj(nodes, edges) {
  const adj = new Map();
  for (const n of nodes) adj.set(n.nodeId, []);
  for (const e of edges) {
    if (!adj.has(e.from) || !adj.has(e.to)) continue;
    const w = typeof e.weight === 'number' ? e.weight : 1;
    adj.get(e.from).push({ to: e.to, w });
    adj.get(e.to).push({ to: e.from, w }); // mặc định undirected
  }
  return adj;
}

function dijkstra(adj, start, goal) {
  const dist = new Map();
  const prev = new Map();
  const visited = new Set();
  const pq = []; // [d, node]

  dist.set(start, 0);
  pq.push([0, start]);

  while (pq.length) {
    pq.sort((a, b) => a[0] - b[0]);
    const [d, u] = pq.shift();
    if (visited.has(u)) continue;
    visited.add(u);
    if (u === goal) break;

    for (const { to, w } of adj.get(u) || []) {
      if (visited.has(to)) continue;
      const nd = d + w;
      if (!dist.has(to) || nd < dist.get(to)) {
        dist.set(to, nd);
        prev.set(to, u);
        pq.push([nd, to]);
      }
    }
  }

  if (!dist.has(goal)) return { distance: null, path: [] };

  const path = [];
  let cur = goal;
  while (cur) {
    path.push(cur);
    cur = prev.get(cur);
    if (cur === start) {
      path.push(start);
      break;
    }
  }
  path.reverse();
  return { distance: dist.get(goal), path };
}

function findNearestNode(nodes, x, y, kind = 'checkpoint') {
  const target = { x: Number(x), y: Number(y) };
  const candidates = nodes.filter((n) => !kind || n.kind === kind);
  if (!candidates.length) return null;
  candidates.sort((a, b) => dist2(a, target) - dist2(b, target));
  return candidates[0];
}

function resolveDestinationNode(nodes, { toNodeId, bedId, stationId, rfidUid }) {
  if (toNodeId) return nodes.find((n) => n.nodeId === String(toNodeId));
  if (bedId) return nodes.find((n) => n.kind === 'bed' && String(n.bedId) === String(bedId));
  if (stationId) return nodes.find((n) => n.kind === 'station' && String(n.stationId) === String(stationId));
  if (rfidUid) {
    const uid = normUid(rfidUid);
    return nodes.find((n) => n.kind === 'checkpoint' && normUid(n.rfidUid) === uid);
  }
  return null;
}

function computeEdgeWeights(nodes = [], edges = []) {
  const m = new Map(nodes.map((n) => [n.nodeId, n]));
  return edges.map((e) => {
    if (typeof e.weight === 'number') return e;
    const a = m.get(e.from);
    const b = m.get(e.to);
    if (!a || !b) return { ...e, weight: 1 };
    const dx = a.coordinates.x - b.coordinates.x;
    const dy = a.coordinates.y - b.coordinates.y;
    return { ...e, weight: Math.sqrt(dx * dx + dy * dy) };
  });
}

// =========================
// CRUD tối thiểu
// =========================

/** Tạo map (metadata) */
r.post('/', async (req, res) => {
  try {
    const { mapId, name, building, floor, imageUrl } = req.body || {};
    if (!mapId || !building || !floor) return res.status(400).json({ message: 'mapId, building, floor required' });

    const exists = await MapGraph.exists({ mapId: String(mapId) });
    if (exists) return res.status(400).json({ message: 'mapId already exists' });

    const created = await MapGraph.create({ mapId: String(mapId), name, building, floor, imageUrl });
    res.status(201).json(created);
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

/** Import node/edge một lần (seed dữ liệu) */
r.post('/:mapId/import', async (req, res) => {
  try {
    const mapId = String(req.params.mapId);
    const { name, building, floor, imageUrl, nodes, edges } = req.body || {};

    const safeNodes = Array.isArray(nodes) ? nodes : [];
    const safeEdges = computeEdgeWeights(safeNodes, Array.isArray(edges) ? edges : []);

    const doc = await MapGraph.findOneAndUpdate(
      { mapId },
      { mapId, name, building, floor, imageUrl, nodes: safeNodes, edges: safeEdges },
      { upsert: true, new: true }
    );

    res.json({ ok: true, map: doc });
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

/** Lấy map */
r.get('/:mapId', async (req, res) => {
  const map = await MapGraph.findOne({ mapId: String(req.params.mapId) }).lean();
  if (!map) return res.status(404).json({ message: 'Map not found' });
  res.json(map);
});

// =========================
// Routing
// =========================

/**
 * GET /api/maps/:mapId/route
 * Query:
 * - fromNodeId hoặc fromX,fromY (snap về checkpoint gần nhất)
 * - toNodeId hoặc bedId hoặc stationId hoặc rfidUid
 */
r.get('/:mapId/route', async (req, res) => {
  try {
    const map = await MapGraph.findOne({ mapId: String(req.params.mapId) }).lean();
    if (!map) return res.status(404).json({ message: 'Map not found' });

    const nodes = map.nodes || [];
    const edges = map.edges || [];

    // from
    let fromNode = null;
    if (req.query.fromNodeId) {
      fromNode = nodes.find((n) => n.nodeId === String(req.query.fromNodeId));
    } else if (req.query.fromX !== undefined && req.query.fromY !== undefined) {
      fromNode = findNearestNode(nodes, req.query.fromX, req.query.fromY, 'checkpoint');
    }
    if (!fromNode) return res.status(400).json({ message: 'fromNodeId OR fromX/fromY required (and must resolve)' });

    // to
    const toNode = resolveDestinationNode(nodes, {
      toNodeId: req.query.toNodeId,
      bedId: req.query.bedId,
      stationId: req.query.stationId,
      rfidUid: req.query.rfidUid
    });
    if (!toNode) return res.status(400).json({ message: 'toNodeId OR bedId OR stationId OR rfidUid required (and must resolve)' });

    const adj = buildAdj(nodes, edges);
    const { distance, path } = dijkstra(adj, fromNode.nodeId, toNode.nodeId);
    if (!path.length) return res.status(404).json({ message: 'No route found' });

    const nodeMap = new Map(nodes.map((n) => [n.nodeId, n]));
    const route = path.map((id) => {
      const n = nodeMap.get(id);
      return {
        nodeId: n.nodeId,
        kind: n.kind,
        label: n.label,
        bedId: n.bedId,
        stationId: n.stationId,
        rfidUid: n.rfidUid,
        x: n.coordinates.x,
        y: n.coordinates.y,
        floor: map.floor,
        building: map.building
      };
    });

    res.json({
      mapId: map.mapId,
      building: map.building,
      floor: map.floor,
      fromNodeId: fromNode.nodeId,
      toNodeId: toNode.nodeId,
      distance,
      route
    });
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

export default r;
