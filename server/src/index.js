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

app.use('/api/users', usersRouter);
app.use('/api/events', eventsRouter);

// Serve dashboard tĩnh (đặt file index.html trong /public)
app.use(express.static('public'));

const port = process.env.PORT || 3000;
connectDB().then(() => {
  app.listen(port, () => console.log('Server running on port', port));
});
