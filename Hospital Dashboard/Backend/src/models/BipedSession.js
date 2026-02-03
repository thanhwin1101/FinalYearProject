import mongoose from 'mongoose';

const bipedSessionSchema = new mongoose.Schema({
  sessionId: {
    type: String,
    required: true,
    unique: true,
    index: true
  },
  robotId: {
    type: String,
    required: true,
    index: true
  },
  robotName: {
    type: String,
    default: ''
  },
  userId: {
    type: String,
    index: true
  },
  userName: {
    type: String,
    default: ''
  },
  patientId: {
    type: String,
    index: true
  },
  patientName: {
    type: String,
    default: ''
  },
  startTime: {
    type: Date,
    required: true,
    default: Date.now,
    index: true
  },
  endTime: {
    type: Date
  },
  totalSteps: {
    type: Number,
    default: 0
  },
  // Duration in minutes
  duration: {
    type: Number,
    default: 0
  },
  status: {
    type: String,
    enum: ['active', 'completed', 'interrupted'],
    default: 'active',
    index: true
  },
  notes: [{
    type: String
  }],
  // Additional telemetry data
  telemetry: {
    avgHeartRate: Number,
    maxHeartRate: Number,
    minHeartRate: Number,
    caloriesBurned: Number,
    distanceWalked: Number // in meters
  }
}, {
  timestamps: true
});

// Compound indexes for common queries
bipedSessionSchema.index({ robotId: 1, startTime: -1 });
bipedSessionSchema.index({ userId: 1, startTime: -1 });
bipedSessionSchema.index({ patientId: 1, startTime: -1 });

export default mongoose.model('BipedSession', bipedSessionSchema);
