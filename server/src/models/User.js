import mongoose from 'mongoose';

const userSchema = new mongoose.Schema({
  uid: { type: String, unique: true, required: true }, // UID thẻ RFID (in hoa)
  name: { type: String, required: true },
  email: { type: String }
}, { timestamps: true });

export default mongoose.model('User', userSchema);
