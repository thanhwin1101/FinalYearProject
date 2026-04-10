import mongoose from 'mongoose';
import dotenv from 'dotenv';
dotenv.config();

const DEFAULT_URI = 'mongodb://127.0.0.1:27017/hospital';

export async function connectDB() {
  const uri = process.env.MONGO_URI || DEFAULT_URI;
  if (!process.env.MONGO_URI) {
    console.warn('[db] MONGO_URI not set — using', DEFAULT_URI);
  }
  try {
    await mongoose.connect(uri, {
      serverSelectionTimeoutMS: 8000
    });
    console.log('MongoDB connected:', uri.replace(/\/\/([^:]+):[^@]+@/, '//***:***@'));
  } catch (err) {
    console.error('\n[db] MongoDB connection failed.');
    console.error('    URI:', uri.replace(/\/\/([^:]+):[^@]+@/, '//***:***@'));
    if (err?.message?.includes('ECONNREFUSED') || err?.cause?.code === 'ECONNREFUSED') {
      console.error('    Cause: nothing is listening on the host/port (MongoDB not running).');
      console.error('    Fix: start MongoDB locally, or set MONGO_URI in Backend/.env (e.g. MongoDB Atlas).');
      console.error('    Windows: install MongoDB Community, then run services.msc → start "MongoDB".');
      console.error('    Or:  docker run -d -p 27017:27017 --name mongo mongo:7\n');
    }
    throw err;
  }
}
