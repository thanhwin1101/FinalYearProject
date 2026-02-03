import mongoose from 'mongoose';

const recoverySessionSchema = new mongoose.Schema({
  sessionId: { type: String, required: true, unique: true },
  patientId: { type: mongoose.Schema.Types.ObjectId, ref: 'Patient', required: true },
  bipedRobotId: { type: String, required: true },
  
  // Thông tin phiên
  type: {
    type: String,
    enum: ['gait_training', 'balance_training', 'strength_training', 'mobility_assistance'],
    required: true
  },
  
  programName: { type: String },
  therapist: { type: String }, // Bác sĩ vật lý trị liệu
  
  // Thông số
  targetDuration: { type: Number, required: true }, // phút
  difficultyLevel: { type: Number, min: 1, max: 5, default: 1 },
  targetSteps: { type: Number }, // Số bước mục tiêu
  targetDistance: { type: Number }, // mét
  
  // Tiến độ
  actualDuration: { type: Number }, // phút
  stepsCompleted: { type: Number },
  distanceCovered: { type: Number },
  progress: { type: Number, min: 0, max: 100 },
  
  // Đánh giá
  patientFeedback: {
    painLevel: { type: Number, min: 0, max: 10 },
    comfortLevel: { type: Number, min: 1, max: 5 },
    notes: { type: String }
  },
  
  therapistAssessment: {
    stabilityScore: { type: Number, min: 1, max: 10 },
    enduranceScore: { type: Number, min: 1, max: 10 },
    notes: { type: String },
    recommendations: { type: String }
  },
  
  // Trạng thái
  status: {
    type: String,
    enum: ['scheduled', 'in_progress', 'completed', 'cancelled', 'interrupted'],
    default: 'scheduled'
  },
  
  // Thời gian
  scheduledAt: { type: Date, required: true },
  startedAt: { type: Date },
  completedAt: { type: Date },
  
  // Trường hợp bị gián đoạn (pin yếu)
  interruptedAt: { type: Date },
  interruptionReason: { type: String },
  rescueCarryRobotId: { type: String },
  resumedAt: { type: Date },
  
  // Dữ liệu cảm biến
  sensorData: [{
    timestamp: Date,
    stability: Number,
    weightDistribution: {
      left: Number,
      right: Number
    },
    stepPattern: String,
    balanceScore: Number
  }],
  
  notes: [{ type: String }]
}, { timestamps: true });

export default mongoose.model('RecoverySession', recoverySessionSchema);