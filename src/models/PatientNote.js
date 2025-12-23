import mongoose from 'mongoose';

const patientNoteSchema = new mongoose.Schema({
  patientId: { type: mongoose.Schema.Types.ObjectId, ref: 'Patient', required: true, index: true },
  text: { type: String, required: true, trim: true },
  createdBy: { type: String, trim: true }
}, { timestamps: true });

export default mongoose.model('PatientNote', patientNoteSchema);
