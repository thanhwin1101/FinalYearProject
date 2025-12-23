import mongoose from 'mongoose';

const eventSchema = new mongoose.Schema({
  uid: { type: String, required: true },
  // thời gian server nhận (ưu tiên), nếu muốn dùng timestamp từ thiết bị thì lưu thêm field deviceTs
  at: { type: Date, default: Date.now },
  deviceTs: { type: Number } // millis từ ESP32 (tùy chọn)
}, { timestamps: true });

eventSchema.index({ uid: 1, at: 1 });

export default mongoose.model('Event', eventSchema);
