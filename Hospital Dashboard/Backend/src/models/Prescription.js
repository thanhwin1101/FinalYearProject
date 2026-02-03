import mongoose from 'mongoose';

const prescriptionSchema = new mongoose.Schema({
  patientId: { type: mongoose.Schema.Types.ObjectId, ref: 'Patient', required: true, index: true },

  medication: { type: String, required: true, trim: true },
  dosage: { type: String, required: true, trim: true },
  frequency: { type: String, required: true, trim: true },

  startDate: { type: Date },
  endDate: { type: Date },

  instructions: { type: String, trim: true },
  prescribedBy: { type: String, trim: true },

  status: { type: String, default: 'Active', trim: true } // Active | Stopped | Completed
}, { timestamps: true });

export default mongoose.model('Prescription', prescriptionSchema);
