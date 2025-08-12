import { Router } from 'express';
import mongoose from 'mongoose';
import User from '../models/User.js';
import Event from '../models/Event.js';

const r = Router();

/** Tạo/Cập nhật user (đăng ký thẻ) */
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

/** Danh sách user */
r.get('/', async (_req, res) => {
  const users = await User.find().sort({ name: 1 });
  res.json(users);
});

/** Kiểm tra UID đã đăng ký chưa */
r.get('/:uid/exists', async (req, res) => {
  const uid = String(req.params.uid || '').toUpperCase();
  const exists = await User.exists({ uid });
  res.json({ uid, exists: !!exists });
});

/** XÓA: User + toàn bộ Events của user đó (transaction) */
r.delete('/:uid', async (req, res) => {
  try {
    const normUid = String(req.params.uid || '').toUpperCase();
    if (!normUid) return res.status(400).json({ message: 'uid required' });

    // Kiểm tra tồn tại user trước
    const user = await User.findOne({ uid: normUid });
    if (!user) return res.status(404).json({ ok: false, message: 'User not found' });

    // 1) Xóa toàn bộ events của user
    const evResult = await Event.deleteMany({ uid: normUid });

    // 2) Xóa user
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

export default r;
