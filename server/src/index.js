import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';
import { connectDB } from './db.js';
import usersRouter from './routes/users.js';
import eventsRouter from './routes/events.js';

dotenv.config();
const app = express();

app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true })); // cho phép form-urlencoded

// Log request đơn giản
app.use((req, _res, next) => {
  console.log('[REQ]', req.method, req.url);
  next();
});

app.use('/api/users', usersRouter);
app.use('/api/events', eventsRouter);

// Serve dashboard tĩnh
app.use(express.static('public'));

const port = process.env.PORT || 3000;

connectDB().then(() => {
  app.listen(port, '0.0.0.0', () => console.log('Server running on', port));
});
