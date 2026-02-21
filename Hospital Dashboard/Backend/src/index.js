import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';
import { connectDB } from './db.js';
import path from 'path';

import usersRouter from './routes/users.js';
import eventsRouter from './routes/events.js';
import patientsRouter from './routes/patients.js';
import robotsRouter from './routes/robots.js';

import mapsRouter from './routes/maps.js';
import missionsRouter from './routes/missions.js';
import alertsRouter from './routes/alerts.js';

// MQTT Service
import { initMqtt } from './services/mqttService.js';

dotenv.config();
const app = express();

app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

app.use((req, _res, next) => {
  console.log('[REQ]', req.method, req.url);
  next();
});

app.use('/api/users', usersRouter);
app.use('/api/events', eventsRouter);
app.use('/api/patients', patientsRouter);
app.use('/api/robots', robotsRouter);

app.use('/api/maps', mapsRouter);
app.use('/api/missions', missionsRouter);
app.use('/api/alerts', alertsRouter);

app.use('/uploads', express.static('uploads'));
app.use(express.static('public'));

// robot dashboard route
app.get('/robot-dashboard.html', (_req, res) => {
  res.sendFile(path.join(process.cwd(), 'public', 'robot-dashboard.html'));
});
app.get('/robots', (_req, res) => res.redirect('/robot-dashboard.html'));

const port = process.env.PORT || 3000;

connectDB().then(async () => {
  // Initialize MQTT connection
  initMqtt();
  
  app.listen(port, '0.0.0.0', () => console.log('Server running on', port));
});
