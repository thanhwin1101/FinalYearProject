import express from 'express';
import cors from 'cors';
import dotenv from 'dotenv';
import fs from 'fs';
import { connectDB } from './db.js';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

import usersRouter from './routes/users.js';
import eventsRouter from './routes/events.js';
import patientsRouter from './routes/patients.js';
import robotsRouter, { emitRobotPosition } from './routes/robots.js';

import mapsRouter from './routes/maps.js';
import missionsRouter from './routes/missions.js';
import alertsRouter from './routes/alerts.js';

import { initMqtt, setRobotPositionEmitter } from './services/mqttService.js';

dotenv.config();
const app = express();

/** Build Vite — luôn tính từ file này (Backend/src), không phụ thuộc cwd khi chạy node). */
const frontendDist = path.resolve(__dirname, '../../Frontend/dist');

function sendDashboardIndex(req, res) {
  const indexFile = path.join(frontendDist, 'index.html');
  if (!fs.existsSync(indexFile)) {
    return res
      .status(503)
      .type('text/plain')
      .send(
        'Dashboard chưa build. Chạy: cd "Hospital Dashboard/Frontend" && npm run build'
      );
  }
  res.sendFile(indexFile);
}

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

if (!fs.existsSync(path.join(frontendDist, 'index.html'))) {
  console.warn('[WEB] No Frontend/dist/index.html — run: cd "../Frontend" && npm run build');
} else {
  console.log('[WEB] Serving SPA from', frontendDist);
}
app.use(express.static(frontendDist, { index: 'index.html' }));

app.get('/robot-dashboard.html', (_req, res) => res.redirect(301, '/'));
app.get('/robots', (_req, res) => res.redirect(301, '/'));

/* Express 4: không dùng app.get('*') — thường không match; dùng middleware cuối. */
app.use((req, res, next) => {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    return next();
  }
  if (req.path.startsWith('/api') || req.path.startsWith('/uploads')) {
    return next();
  }
  sendDashboardIndex(req, res);
});

const port = process.env.PORT || 3000;

connectDB()
  .then(async () => {
    setRobotPositionEmitter(emitRobotPosition);
    initMqtt();

    app.listen(port, '0.0.0.0', () => {
      console.log('Server running on', port);
      console.log('Dashboard (SPA): http://localhost:' + port + '/');
    });
  })
  .catch(() => {
    console.error('[boot] Stopping: database unavailable.');
    process.exit(1);
  });
