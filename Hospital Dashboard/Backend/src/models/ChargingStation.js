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
  
  capacity: { type: Number, default: 4 }, // Số robot sạc cùng lúc
  availableSlots: { type: Number, default: 4 },
  status: { 
    type: String, 
    enum: ['active', 'maintenance', 'full', 'offline'],
    default: 'active'
  },
  
  // Thống kê
  totalCharges: { type: Number, default: 0 },
  averageChargingTime: { type: Number, default: 0 }, // phút
  
  // Robot đang sạc
  chargingRobots: [{
    robotId: { type: String },
    startedAt: { type: Date },
    estimatedCompletion: { type: Date },
    initialBattery: { type: Number },
    targetBattery: { type: Number, default: 100 }
  }],
  
  // Lịch sử
  chargingHistory: [{
    robotId: String,
    startedAt: Date,
    completedAt: Date,
    batteryBefore: Number,
    batteryAfter: Number,
    duration: Number // phút
  }]
}, { timestamps: true });

export default mongoose.model('ChargingStation', chargingStationSchema);