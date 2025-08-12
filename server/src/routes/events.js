import { Router } from 'express';
import Event from '../models/Event.js';
import { dailyCountPipeline } from '../utils/agg.js';

const r = Router();

// ESP32 gửi sự kiện bấm nút
r.post('/button', async (req, res) => {
  try {
    const { uid, timestamp } = req.body;
    if (!uid) return res.status(400).json({ message: 'uid required' });

    const doc = await Event.create({
      uid: uid.toUpperCase(),
      at: new Date(),          // thời điểm server nhận (ổn định)
      deviceTs: Number(timestamp) || undefined
    });
    res.status(201).json({ ok: true, id: doc._id });
  } catch (e) { res.status(500).json({ message: e.message }); }
});

// Thống kê theo ngày (phạm vi từ–đến, lọc theo uid)
r.get('/stats/daily', async (req, res) => {
  try {
    const { uid, from, to } = req.query;
    const pipe = dailyCountPipeline({ uid, from, to });
    const data = await Event.aggregate(pipe);
    res.json(data); // [{_id: '2025-08-12', count: 10}, ...]
  } catch (e) { res.status(500).json({ message: e.message }); }
});
// [NEW] Thống kê theo ngày và theo user (để vẽ stacked chart khi chọn "Tất cả")
r.get('/stats/daily-by-user', async (req, res) => {
  try {
    const { from, to } = req.query;

    const match = {};
    if (from || to) {
      match.at = {};
      if (from) match.at.$gte = new Date(from);
      if (to) {
        const end = new Date(to);
        end.setHours(23, 59, 59, 999);
        match.at.$lte = end;
      }
    }

    const data = await Event.aggregate([
      { $match: match },
      {
        $group: {
          _id: {
            day: { $dateToString: { format: '%Y-%m-%d', date: '$at' } },
            uid: '$uid'
          },
          count: { $sum: 1 }
        }
      },
      { $sort: { '_id.day': 1, '_id.uid': 1 } }
    ]);

    res.json(data); // [{ _id:{day:'2025-08-12', uid:'0478...'}, count: 8 }, ...]
  } catch (e) {
    res.status(500).json({ message: e.message });
  }
});

export default r;
