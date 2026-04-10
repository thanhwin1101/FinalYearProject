import { Router } from 'express';
import User from '../models/User.js';
import Event from '../models/Event.js';

const r = Router();

r.post('/', async (req, res) => {
  try {
    const uid = String(req.body?.uid || '').toUpperCase();
    const name = String(req.body?.name || '').trim();
    const email = req.body?.email ? String(req.body.email).trim() : undefined;

    if (!uid || !name) return res.status(400).json({ message: 'uid & name required' });
    const user = await User.findOneAndUpdate(
      { uid },
      { uid, name, email },
      { upsert: true, new: true }
    );
    res.json(user);
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

r.get('/', async (_req, res) => {
  const users = await User.find().sort({ name: 1 });
  res.json(users);
});

r.get('/:uid/exists', async (req, res) => {
  const uid = String(req.params.uid || '').toUpperCase();
  const exists = await User.exists({ uid });
  res.json({ uid, exists: !!exists });
});

r.delete('/:uid', async (req, res) => {
  try {
    const normUid = String(req.params.uid || '').toUpperCase();
    if (!normUid) return res.status(400).json({ message: 'uid required' });

    const user = await User.findOne({ uid: normUid });
    if (!user) return res.status(404).json({ ok: false, message: 'User not found' });

    const evResult = await Event.deleteMany({ uid: normUid });
    const uRes = await User.deleteOne({ uid: normUid });

    res.json({
      ok: true,
      uid: normUid,
      deletedUser: uRes.deletedCount === 1,
      deletedEvents: evResult.deletedCount || 0
    });
  } catch (e) {
    res.status(500).json({ ok: false, message: e.message || 'Delete failed' });
  }
});

r.get('/:uid/detail', async (req, res) => {
  const uid = String(req.params.uid || '').toUpperCase();
  const user = await User.findOne({ uid });
  if (!user) return res.status(404).json({ message: 'User not found' });
  res.json({ uid: user.uid, name: user.name, email: user.email || null });
});

export default r;
