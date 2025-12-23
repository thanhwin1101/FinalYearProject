import mongoose from 'mongoose';
import dotenv from 'dotenv';
dotenv.config();

export async function connectDB() {
  await mongoose.connect(process.env.MONGO_URI, {
    serverSelectionTimeoutMS: 5000
  });
  console.log('MongoDB connected');
}
