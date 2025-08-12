import { Router } from 'express';
import Event from '../models/Event.js';
import User from '../models/User.js';
import { dailyCountPipeline } from '../utils/agg.js';

const r = Router();

/** ESP32 gửi sự kiện bấm nút – chỉ nhận nếu UID đã đăng ký */
r.post('/button', async (req, res) => {
  try {
    const { uid, timestamp } = req.body || {};
    if (!uid) return res.status(400).json({ message: 'uid required' });

    const normUid = String(uid).toUpperCase();

    // CHẶN UID CHƯA ĐĂNG KÝ
    const exists = await User.exists({ uid: normUid });
    if (!exists) return res.status(403).json({ ok: false, message: 'UID not registered' });

    const doc = await Event.create({
      uid: normUid,
      at: new Date(),                 // lấy thời gian server
      deviceTs: Number(timestamp) || undefined
    });
    res.status(201).json({ ok: true, id: doc._id });
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

/** Thống kê theo ngày (lọc theo uid nếu có) */
r.get('/stats/daily', async (req, res) => {
  try {
    const { uid, from, to } = req.query;
    const pipe = dailyCountPipeline({ uid: uid ? String(uid).toUpperCase() : undefined, from, to });
    const data = await Event.aggregate(pipe);
    res.json(data);
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

/** Thống kê theo ngày & theo user (cho stacked chart khi chọn "Tất cả") */
r.get('/stats/daily-by-user', async (req, res) => {
  try {
    const { from, to } = req.query;

    const match = {};
    if (from || to) {
      match.at = {};
      if (from) match.at.$gte = new Date(from);
      if (to) {
        const end = new Date(to);
        end.setHours(23,59,59,999);
        match.at.$lte = end;
      }
    }

    const data = await Event.aggregate([
      { $match: match },
      { $group: {
          _id: {
            day: { $dateToString: { format: '%Y-%m-%d', date: '$at' } },
            uid: '$uid'
          },
          count: { $sum: 1 }
      }},
      { $sort: { '_id.day': 1, '_id.uid': 1 } }
    ]);

    res.json(data);
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

export default r;
