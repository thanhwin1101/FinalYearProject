import mongoose from 'mongoose';

const timelineSchema = new mongoose.Schema({
  at: { type: Date, default: Date.now },
  title: { type: String, required: true },
  createdBy: { type: String },
  description: { type: String }
}, { _id: true });

const prescriptionSchema = new mongoose.Schema({
  medication: { type: String, required: true },
  dosage: { type: String, required: true },
  frequency: { type: String, required: true },
  startDate: { type: String },
  endDate: { type: String },
  prescribedBy: { type: String },
  instructions: { type: String },
  status: { type: String, default: 'Active' } // Active | Stopped | Completed
}, { _id: true });

const noteSchema = new mongoose.Schema({
  createdAt: { type: Date, default: Date.now },
  createdBy: { type: String },
  text: { type: String, required: true }
}, { _id: true });

const patientSchema = new mongoose.Schema({
  fullName: { type: String, required: true },
  mrn: { type: String, required: true, unique: true },

  dob: { type: String },
  gender: { type: String },

  admissionDate: { type: String, required: true }, // YYYY-MM-DD
  status: { type: String, required: true },

  department: { type: String },
  ward: { type: String },
  roomBed: { type: String },

  primaryDoctor: { type: String, required: true },

  cardNumber: { type: String, required: true },
  relativeName: { type: String, required: true },
  relativePhone: { type: String, required: true },
  insurancePolicyId: { type: String },

  // ✅ 2 field quan trọng để xử lý xóa file
  photoPath: { type: String }, // ví dụ: uploads/patients/xxx.jpg
  photoUrl: { type: String },  // ví dụ: /uploads/patients/xxx.jpg

  timeline: { type: [timelineSchema], default: [] },
  prescriptions: { type: [prescriptionSchema], default: [] },
  notes: { type: [noteSchema], default: [] }
}, { timestamps: true });

export default mongoose.model('Patient', patientSchema);
