# Hospital Management System

A full-stack hospital management system with Patient Care and Robot Navigation features.

## Project Structure

```
Hospital Dashboard/
├── Backend/          # Express.js API server
│   ├── src/
│   │   ├── index.js      # Main entry point
│   │   ├── db.js         # MongoDB connection
│   │   ├── models/       # Mongoose models
│   │   ├── routes/       # API routes
│   │   └── utils/        # Helper functions
│   ├── uploads/          # Patient photos storage
│   └── package.json
│
└── Frontend/         # React + Vite application
    ├── src/
    │   ├── app/
    │   │   ├── api/          # API service layer
    │   │   ├── components/   # React components
    │   │   ├── hooks/        # Custom React hooks
    │   │   ├── types/        # TypeScript types
    │   │   └── utils/        # Helper functions
    │   └── main.tsx
    ├── vite.config.ts
    └── package.json
```

## Prerequisites

- Node.js v18 or higher
- MongoDB (local or remote)
- npm or yarn

## Getting Started

### 1. Setup Backend

```bash
# Navigate to backend directory
cd Backend

# Install dependencies
npm install

# Create .env file
cp .env.example .env

# Edit .env and set your MongoDB connection string
# MONGODB_URI=mongodb://localhost:27017/hospital

# Start the backend server
npm run dev
```

The backend will run on `http://localhost:3000`

### 2. Setup Frontend

```bash
# Navigate to frontend directory (in a new terminal)
cd Frontend

# Install dependencies
npm install

# Start the development server
npm run dev
```

The frontend will run on `http://localhost:5173`

### 3. Access the Application

Open your browser and go to: `http://localhost:5173`

## API Endpoints

### Patients API
- `GET /api/patients` - List all patients
- `GET /api/patients/:id` - Get patient by ID
- `GET /api/patients/mrn/:mrn` - Get patient by MRN
- `GET /api/patients/card/:cardNumber` - Get patient by RFID card
- `POST /api/patients` - Create new patient (supports multipart/form-data for photo upload)
- `PUT /api/patients/:id` - Update patient
- `DELETE /api/patients/:id` - Delete patient
- `GET /api/patients/beds` - Get all beds
- `GET /api/patients/occupancy` - Get bed occupancy info

### Robots API
- `GET /api/robots/carry/status` - Get carry robots status
- `GET /api/robots/biped/active` - Get biped robots status
- `PUT /api/robots/:id/telemetry` - Update robot telemetry (from ESP32)

### Missions API
- `GET /api/missions/transport` - List transport missions
- `POST /api/missions/transport` - Create new transport mission
- `PUT /api/missions/transport/:id/cancel` - Cancel mission
- `PUT /api/missions/transport/:id/arrived` - Mark mission as arrived
- `PUT /api/missions/transport/:id/returned` - Mark mission as completed

### Maps API
- `GET /api/maps/:mapId` - Get map by ID
- `PUT /api/maps/:mapId` - Create/update map
- `POST /api/maps/:mapId/route` - Calculate route

### Alerts API
- `GET /api/alerts` - List alerts
- `POST /api/alerts` - Create alert
- `PUT /api/alerts/:id/resolve` - Resolve alert

### Users API (RFID Cards)
- `GET /api/users` - List users
- `POST /api/users` - Register user/card
- `GET /api/users/:uid/exists` - Check if UID exists
- `DELETE /api/users/:uid` - Delete user

### Events API
- `POST /api/events/button` - Create button press event
- `GET /api/events/stats/daily` - Get daily statistics

## Features

### Patient Management
- Patient registration with photo capture
- RFID card integration
- Bed assignment and tracking
- Status management (Stable, Critical, Recovering, etc.)
- Search and filter patients
- Export to CSV

### Robot Navigation Center
- Real-time robot status monitoring
- Bed map visualization
- Mission creation and tracking
- Task cancellation

### Integration with Hardware
- ESP32 robot telemetry integration
- RFID card reader support
- Camera capture for patient photos

## Environment Variables

### Backend (.env)
```
MONGO_URI=mongodb://localhost:27017/hospital
PORT=3000
NODE_ENV=development
```

### Frontend (.env)
```
# Only needed for production - uses proxy in development
VITE_API_URL=
```

## Development

### Backend Development
The backend uses Express.js with MongoDB/Mongoose. Key files:
- `src/index.js` - Server setup and middleware
- `src/routes/` - API route handlers
- `src/models/` - Mongoose schemas

### Frontend Development
The frontend uses React with TypeScript and Vite. Key files:
- `src/app/api/` - API service layer
- `src/app/hooks/` - Custom React hooks for data fetching
- `src/app/components/` - React UI components

The Vite development server proxies API requests to the backend automatically.

## Troubleshooting

### "Failed to fetch patients" error
1. Make sure the backend is running on port 3000
2. Check MongoDB connection in backend console
3. Check browser console for detailed errors

### CORS issues
The Vite dev server proxies requests to avoid CORS issues. If you still have problems:
1. Make sure you're accessing the app via `http://localhost:5173`
2. Check that the backend CORS middleware is enabled

### Photo upload not working
1. Make sure the `uploads/patients/` directory exists in Backend
2. Check file size limit (5MB max)
3. Only image files (jpg, png, webp) are allowed
