import mongoose from 'mongoose';

const chargingStationSchema = new mongoose.Schema({
  stationId: { type: String, required: true, unique: true },
  name: { type: String, required: true },
  location: {
    building: { type: String, required: true },
    floor: { type: String, required: true },
    room: { type: String },
    coordinates: {
      x: { type: Number, required: true },
      y: { type: Number, required: true }
    }
  },

  capacity: { type: Number, default: 4 },
  availableSlots: { type: Number, default: 4 },
  status: {
    type: String,
    enum: ['active', 'maintenance', 'full', 'offline'],
    default: 'active'
  },

  totalCharges: { type: Number, default: 0 },
  averageChargingTime: { type: Number, default: 0 },

  chargingRobots: [{
    robotId: { type: String },
    startedAt: { type: Date },
    estimatedCompletion: { type: Date },
    initialBattery: { type: Number },
    targetBattery: { type: Number, default: 100 }
  }],

  chargingHistory: [{
    robotId: String,
    startedAt: Date,
    completedAt: Date,
    batteryBefore: Number,
    batteryAfter: Number,
    duration: Number
  }]
}, { timestamps: true });

export default mongoose.model('ChargingStation', chargingStationSchema);
