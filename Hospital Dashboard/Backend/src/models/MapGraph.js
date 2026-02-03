import mongoose from 'mongoose';

// Graph map cho 1 tầng (1 floor) để carry robot định tuyến theo checkpoint RFID.
// - nodes: checkpoint/bed/station/room với toạ độ (x,y) theo hệ trục của ảnh map.
// - edges: kết nối giữa node (mặc định undirected), weight mặc định = khoảng cách Euclid.

const nodeSchema = new mongoose.Schema(
  {
    nodeId: { type: String, required: true },
    kind: { type: String, required: true, enum: ['checkpoint', 'bed', 'station', 'room'] },
    label: { type: String },

    // RFID UID gắn tại checkpoint (nếu có)
    rfidUid: { type: String },

    // business ids
    bedId: { type: String },
    stationId: { type: String },

    coordinates: {
      x: { type: Number, required: true },
      y: { type: Number, required: true }
    }
  },
  { _id: false }
);

const edgeSchema = new mongoose.Schema(
  {
    from: { type: String, required: true },
    to: { type: String, required: true },
    weight: { type: Number } // nếu không truyền sẽ tự tính
  },
  { _id: false }
);

const mapGraphSchema = new mongoose.Schema(
  {
    mapId: { type: String, required: true, unique: true },
    name: { type: String },

    building: { type: String, required: true },
    floor: { type: String, required: true },

    // ảnh nền map để dashboard render (đặt trong /public và trỏ bằng URL)
    imageUrl: { type: String },

    nodes: { type: [nodeSchema], default: [] },
    edges: { type: [edgeSchema], default: [] }
  },
  { timestamps: true }
);

function dist(a, b) {
  const dx = (a?.coordinates?.x ?? 0) - (b?.coordinates?.x ?? 0);
  const dy = (a?.coordinates?.y ?? 0) - (b?.coordinates?.y ?? 0);
  return Math.sqrt(dx * dx + dy * dy);
}

// Auto weight cho edges nếu thiếu (chỉ áp dụng khi .save())
mapGraphSchema.pre('save', function (next) {
  try {
    const nodeMap = new Map((this.nodes || []).map((n) => [n.nodeId, n]));
    for (const e of this.edges || []) {
      if (typeof e.weight === 'number') continue;
      const a = nodeMap.get(e.from);
      const b = nodeMap.get(e.to);
      if (a && b) e.weight = dist(a, b);
    }
    next();
  } catch (err) {
    next(err);
  }
});

export default mongoose.model('MapGraph', mapGraphSchema);
