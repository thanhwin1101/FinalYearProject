import mongoose from 'mongoose';

const routePointSchema = new mongoose.Schema({
  nodeId: { type: String, required: true },

  x: { type: Number, required: true },
  y: { type: Number, required: true },

  floor: { type: String },
  building: { type: String },

  rfidUid: { type: String, default: null },
  kind: { type: String, default: '' },
  label: { type: String, default: '' },

  // Firmware reads only this field currently
  action: {
    type: String,
    enum: ['F', 'L', 'R'],
    default: 'F'
  },

  // Optional future-proof (not required by current firmware)
  actions: {
    type: [{ type: String, enum: ['F', 'L', 'R'] }],
    default: []
  }
}, { _id: false });

const transportMissionSchema = new mongoose.Schema({
  missionId: { type: String, required: true, unique: true },
  mapId: { type: String, required: true },

  carryRobotId: { type: String, required: true },
  requestedByUid: { type: String },

  patientName: { type: String, default: '' },

  bedId: { type: String, required: true }, // R1M1..R4O3
  destinationNodeId: { type: String },

  outboundRoute: { type: [routePointSchema], default: [] },
  returnRoute: { type: [routePointSchema], default: [] },

  // legacy compatibility
  plannedRoute: { type: [routePointSchema], default: [] },

  actualRoute: {
    type: [{
      x: Number,
      y: Number,
      floor: String,
      building: String,
      timestamp: Date
    }],
    default: []
  },

  status: {
    type: String,
    enum: ['pending', 'en_route', 'arrived', 'completed', 'failed', 'cancelled'],
    default: 'pending'
  },

  requestedAt: { type: Date, default: Date.now },
  assignedAt: { type: Date },
  startedAt: { type: Date },
  arrivedAt: { type: Date },
  completedAt: { type: Date },

  returnedAt: { type: Date, default: null },
  cancelRequestedAt: { type: Date, default: null },
  cancelledAt: { type: Date, default: null },
  cancelledBy: { type: String, default: null },

  lowBatteryAlerted: { type: Boolean, default: false },
  notes: [{ type: String }]
}, { timestamps: true });

transportMissionSchema.index({ carryRobotId: 1, requestedAt: -1 });
transportMissionSchema.index({ status: 1, requestedAt: -1 });

export default mongoose.model('TransportMission', transportMissionSchema);
