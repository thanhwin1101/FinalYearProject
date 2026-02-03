import { Router } from 'express';
import Alert from '../models/Alert.js';

const r = Router();

/** GET /api/alerts?active=1&limit=50 */
r.get('/', async (req, res) => {
  const active = String(req.query.active || '') === '1';
  const limit = Math.min(Number(req.query.limit) || 50, 200);

  const filter = active ? { resolvedAt: { $exists: false } } : {};
  const items = await Alert.find(filter).sort({ createdAt: -1 }).limit(limit);
  res.json(items);
});

/** POST /api/alerts */
r.post('/', async (req, res) => {
  try {
    const { type, level, robotId, missionId, message, data } = req.body || {};
    if (!type || !message) return res.status(400).json({ message: 'type & message required' });
    const created = await Alert.create({ type, level, robotId, missionId, message, data });
    res.status(201).json(created);
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

/** PUT /api/alerts/:id/resolve */
r.put('/:id/resolve', async (req, res) => {
  try {
    const item = await Alert.findById(req.params.id);
    if (!item) return res.status(404).json({ message: 'Alert not found' });
    item.resolvedAt = new Date();
    await item.save();
    res.json({ ok: true, alert: item });
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

export default r;
