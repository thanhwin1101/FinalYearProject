import mongoose from 'mongoose';

const robotSchema = new mongoose.Schema({
  robotId: { type: String, required: true, unique: true },
  name: { type: String, required: true },
  type: { type: String, required: true, enum: ['carry'] },
  model: { type: String },

  status: {
    type: String,
    default: 'idle',
    enum: ['idle', 'busy', 'charging', 'maintenance', 'offline', 'low_battery']
  },

  lastSeenAt: { type: Date, default: null },

  currentLocation: {
    building: { type: String },
    floor: { type: String },
    room: { type: String },
    coordinates: { x: { type: Number }, y: { type: Number } }
  },

  nearestChargingStation: {
    stationId: { type: String },
    location: {
      building: String,
      floor: String,
      room: String,
      coordinates: { x: Number, y: Number }
    },
    distance: { type: Number }
  },

  batteryLevel: { type: Number, min: 0, max: 100, default: 100 },
  firmwareVersion: { type: String },
  lastMaintenance: { type: Date },
  nextMaintenance: { type: Date },

  transportData: {
    carryingItem: { type: String },
    weight: { type: Number },
    destination: { building: String, floor: String, room: String },
    estimatedArrival: { type: Date }
  },

  totalDeliveries: { type: Number, default: 0 },
  totalDistance: { type: Number, default: 0 },
  totalOperatingHours: { type: Number, default: 0 },

  notes: [{ type: String }]
}, { timestamps: true });

robotSchema.index({ type: 1, status: 1 });
robotSchema.index({ batteryLevel: 1 });
robotSchema.index({ lastSeenAt: -1 });

export default mongoose.model('Robot', robotSchema);
