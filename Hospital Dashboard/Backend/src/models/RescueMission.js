import mongoose from 'mongoose';

const rescueMissionSchema = new mongoose.Schema({
  missionId: { type: String, required: true, unique: true },
  bipedRobotId: { type: String, required: true },
  carryRobotId: { type: String },
  
  // Vị trí
  rescueLocation: {
    building: String,
    floor: String,
    room: String,
    coordinates: { x: Number, y: Number }
  },
  
  chargingStation: {
    stationId: String,
    location: {
      building: String,
      floor: String,
      room: String,
      coordinates: { x: Number, y: Number }
    }
  },
  
  // Tuyến đường
  plannedRoute: [{
    x: Number,
    y: Number,
    floor: String,
    building: String
  }],
  
  actualRoute: [{
    x: Number,
    y: Number,
    floor: String,
    building: String,
    timestamp: Date
  }],
  
  // Chi tiết
  batteryLevelAtRescue: { type: Number },
  reason: { 
    type: String,
    enum: ['low_battery', 'mechanical_failure', 'stuck', 'emergency_stop']
  },
  
  // Trạng thái
  status: {
    type: String,
    enum: ['pending', 'en_route', 'arrived', 'loading', 'transporting', 'completed', 'failed'],
    default: 'pending'
  },
  
  // Thời gian
  requestedAt: { type: Date, default: Date.now },
  assignedAt: { type: Date },
  arrivedAt: { type: Date },
  loadingStartedAt: { type: Date },
  transportStartedAt: { type: Date },
  completedAt: { type: Date },
  
  // Hiệu suất
  totalDistance: { type: Number }, // mét
  totalTime: { type: Number }, // giây
  
  // Đánh giá
  efficiencyScore: { type: Number, min: 1, max: 10 },
  notes: [{ type: String }]
}, { timestamps: true });

export default mongoose.model('RescueMission', rescueMissionSchema);