import mongoose from 'mongoose';

const eventSchema = new mongoose.Schema({
  uid: { type: String, required: true },

  at: { type: Date, default: Date.now },
  deviceTs: { type: Number }
}, { timestamps: true });

eventSchema.index({ uid: 1, at: 1 });

export default mongoose.model('Event', eventSchema);
