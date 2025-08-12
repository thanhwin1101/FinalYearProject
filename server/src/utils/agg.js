export function dailyCountPipeline({ uid, from, to }) {
  const match = {};
  if (uid) match.uid = uid;
  if (from || to) {
    match.at = {};
    if (from) match.at.$gte = new Date(from);
    if (to) {
      // to inclusive đến hết ngày
      const end = new Date(to);
      end.setHours(23,59,59,999);
      match.at.$lte = end;
    }
  }
  return [
    { $match: match },
    { $group: {
        _id: { $dateToString: { format: "%Y-%m-%d", date: "$at" } },
        count: { $sum: 1 }
    }},
    { $sort: { "_id": 1 } }
  ];
}
