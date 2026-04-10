import mongoose from 'mongoose';

const alertSchema = new mongoose.Schema(
  {
    type: {
      type: String,
      required: true,
      enum: [
        'carry_low_battery',
        'battery_warning',
        'battery_critical',
        'mission_rejected_low_battery',
        'rescue_required',
        'route_deviation',
        'info'
      ]
    },
    level: { type: String, enum: ['low', 'medium', 'high'], default: 'medium' },

    robotId: { type: String },
    missionId: { type: String },
    message: { type: String, required: true },

    data: { type: mongoose.Schema.Types.Mixed },

    createdAt: { type: Date, default: Date.now, index: true },
    resolvedAt: { type: Date }
  },
  { timestamps: true }
);

alertSchema.index({ resolvedAt: 1, createdAt: -1 });
alertSchema.index({ type: 1, createdAt: -1 });

export default mongoose.model('Alert', alertSchema);
