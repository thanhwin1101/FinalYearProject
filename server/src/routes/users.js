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

export default r;
