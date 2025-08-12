import { Router } from 'express';
import User from '../models/User.js';

const r = Router();

// Tạo user và map UID
r.post('/', async (req, res) => {
  try {
    const { uid, name, email } = req.body;
    if (!uid || !name) return res.status(400).json({ message: 'uid & name required' });
    const user = await User.findOneAndUpdate(
      { uid: uid.toUpperCase() },
      { uid: uid.toUpperCase(), name, email },
      { upsert: true, new: true }
    );
    res.json(user);
  } catch (e) { res.status(500).json({ message: e.message }); }
});

// Lấy danh sách user
r.get('/', async (_req, res) => {
  const users = await User.find().sort({ name: 1 });
  res.json(users);
});
// Xóa user theo UID
r.delete('/:uid', async (req, res) => {
  try {
    const uid = String(req.params.uid || '').toUpperCase();
    if (!uid) return res.status(400).json({ message: 'uid required' });

    const deleted = await User.findOneAndDelete({ uid });
    if (!deleted) return res.status(404).json({ message: 'User not found' });

    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

export default r;
