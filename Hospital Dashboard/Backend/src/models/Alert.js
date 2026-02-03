import mongoose from 'mongoose';

// Alert/Notification dùng cho dashboard:
// - carry robot yếu pin + đang vận chuyển tới giường nào
// - biped robot cần cứu hộ, sai checkpoint, v.v.

const alertSchema = new mongoose.Schema(
  {
    type: {
      type: String,
      required: true,
      enum: ['carry_low_battery', 'biped_low_battery', 'rescue_required', 'route_deviation', 'info']
    },
    level: { type: String, enum: ['low', 'medium', 'high'], default: 'medium' },

    robotId: { type: String },
    missionId: { type: String },
    message: { type: String, required: true },

    // Dữ liệu thêm tuỳ trường hợp
    data: { type: mongoose.Schema.Types.Mixed },

    createdAt: { type: Date, default: Date.now, index: true },
    resolvedAt: { type: Date }
  },
  { timestamps: true }
);

alertSchema.index({ resolvedAt: 1, createdAt: -1 });
alertSchema.index({ type: 1, createdAt: -1 });

export default mongoose.model('Alert', alertSchema);
