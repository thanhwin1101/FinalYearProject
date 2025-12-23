import mongoose from 'mongoose';

const patientTimelineSchema = new mongoose.Schema({
  patientId: { type: mongoose.Schema.Types.ObjectId, ref: 'Patient', required: true, index: true },
  at: { type: Date, default: Date.now, index: true },
  title: { type: String, required: true, trim: true },
  description: { type: String, trim: true },
  createdBy: { type: String, trim: true }
}, { timestamps: true });

export default mongoose.model('PatientTimeline', patientTimelineSchema);
