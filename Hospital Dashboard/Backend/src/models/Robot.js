import mongoose from 'mongoose';

const robotSchema = new mongoose.Schema({
  robotId: { type: String, required: true, unique: true },
  name: { type: String, required: true },
  type: { type: String, required: true, enum: ['biped', 'carry'] },
  model: { type: String },

  status: {
    type: String,
    default: 'idle',
    enum: ['idle', 'busy', 'charging', 'maintenance', 'offline', 'low_battery']
  },

  // ✅ để robot-manager xác định online
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

  recoveryData: {
    currentPatient: {
      patientId: { type: mongoose.Schema.Types.ObjectId, ref: 'Patient' },
      assignedAt: { type: Date }
    },
    recoveryProgram: {
      type: String,
      enum: ['gait_training', 'balance_training', 'strength_training', 'mobility_assistance']
    },
    sessionDuration: { type: Number },
    progress: { type: Number, min: 0, max: 100, default: 0 },
    difficultyLevel: { type: Number, min: 1, max: 5, default: 1 }
  },

  transportData: {
    carryingItem: { type: String },
    weight: { type: Number },
    destination: { building: String, floor: String, room: String },
    estimatedArrival: { type: Date }
  },

  totalSessions: { type: Number, default: 0 },
  totalDeliveries: { type: Number, default: 0 },
  totalDistance: { type: Number, default: 0 },
  totalOperatingHours: { type: Number, default: 0 },

  rescueOperations: [{
    rescuedRobotId: { type: String },
    rescuedAt: { type: Date },
    fromLocation: { building: String, floor: String, room: String },
    toLocation: { building: String, floor: String, room: String }
  }],

  notes: [{ type: String }]
}, { timestamps: true });

robotSchema.index({ type: 1, status: 1 });
robotSchema.index({ batteryLevel: 1 });
robotSchema.index({ lastSeenAt: -1 });

// ❌ bỏ index 2dsphere vì không phải GeoJSON
// robotSchema.index({ 'currentLocation.coordinates': '2dsphere' });

export default mongoose.model('Robot', robotSchema);
