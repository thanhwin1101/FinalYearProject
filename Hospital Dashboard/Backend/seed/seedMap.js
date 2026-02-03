/**
 * Seed Map data for Hospital Floor 1
 * Run: node seed/seedMap.js
 */

import mongoose from 'mongoose';
import MapGraph from '../src/models/MapGraph.js';
import { CHECKPOINT_UID } from './checkpointsF1.js';

const MONGO_URI = process.env.MONGO_URI || 'mongodb://127.0.0.1:27017/hospital';

// Build nodes from CHECKPOINT_UID
function buildNodes() {
  const nodes = [];
  
  // Hub/corridor nodes
  nodes.push({ nodeId: 'MED', kind: 'station', label: 'Phòng thuốc', rfidUid: CHECKPOINT_UID.MED, coordinates: { x: 400, y: 500 } });
  nodes.push({ nodeId: 'H_MED', kind: 'checkpoint', label: 'Hub MED', rfidUid: CHECKPOINT_UID.H_MED, coordinates: { x: 400, y: 400 } });
  nodes.push({ nodeId: 'H_BOT', kind: 'checkpoint', label: 'Hub Bottom', rfidUid: CHECKPOINT_UID.H_BOT, coordinates: { x: 400, y: 300 } });
  nodes.push({ nodeId: 'J4', kind: 'checkpoint', label: 'Junction 4', rfidUid: CHECKPOINT_UID.J4, coordinates: { x: 400, y: 200 } });
  nodes.push({ nodeId: 'H_TOP', kind: 'checkpoint', label: 'Hub Top', rfidUid: CHECKPOINT_UID.H_TOP, coordinates: { x: 400, y: 100 } });

  // Room nodes
  for (let room = 1; room <= 4; room++) {
    const baseX = room <= 2 ? (room === 1 ? 100 : 700) : (room === 3 ? 100 : 700);
    const baseY = room <= 2 ? 100 : 300;

    // D1, D2 (door/junction nodes)
    const d1Key = `R${room}D1`;
    const d2Key = `R${room}D2`;
    nodes.push({ nodeId: d1Key, kind: 'checkpoint', label: `Room ${room} Door 1`, rfidUid: CHECKPOINT_UID[d1Key], coordinates: { x: baseX + 50, y: baseY } });
    nodes.push({ nodeId: d2Key, kind: 'checkpoint', label: `Room ${room} Door 2`, rfidUid: CHECKPOINT_UID[d2Key], coordinates: { x: baseX + 100, y: baseY } });

    // M1, M2, M3 (beds on M side)
    for (let i = 1; i <= 3; i++) {
      const key = `R${room}M${i}`;
      nodes.push({
        nodeId: key,
        kind: 'bed',
        bedId: key,
        label: `Room ${room} Bed M${i}`,
        rfidUid: CHECKPOINT_UID[key],
        coordinates: { x: baseX, y: baseY + i * 30 }
      });
    }

    // O1, O2, O3 (beds on O side)
    for (let i = 1; i <= 3; i++) {
      const key = `R${room}O${i}`;
      nodes.push({
        nodeId: key,
        kind: 'bed',
        bedId: key,
        label: `Room ${room} Bed O${i}`,
        rfidUid: CHECKPOINT_UID[key],
        coordinates: { x: baseX + 150, y: baseY + i * 30 }
      });
    }
  }

  return nodes;
}

// Build edges connecting nodes
function buildEdges() {
  const edges = [];

  // Main corridor
  edges.push({ from: 'MED', to: 'H_MED', weight: 1 });
  edges.push({ from: 'H_MED', to: 'H_BOT', weight: 1 });
  edges.push({ from: 'H_BOT', to: 'J4', weight: 1 });
  edges.push({ from: 'J4', to: 'H_TOP', weight: 1 });

  // Room connections
  for (let room = 1; room <= 4; room++) {
    const hubNode = room <= 2 ? 'H_TOP' : 'H_BOT';
    const d1 = `R${room}D1`;
    const d2 = `R${room}D2`;

    // Hub -> D1
    edges.push({ from: hubNode, to: d1, weight: 1 });

    // D1 -> D2
    edges.push({ from: d1, to: d2, weight: 1 });

    // D1 -> M1 -> M2 -> M3
    edges.push({ from: d1, to: `R${room}M1`, weight: 1 });
    edges.push({ from: `R${room}M1`, to: `R${room}M2`, weight: 1 });
    edges.push({ from: `R${room}M2`, to: `R${room}M3`, weight: 1 });

    // D2 -> O1 -> O2 -> O3
    edges.push({ from: d2, to: `R${room}O1`, weight: 1 });
    edges.push({ from: `R${room}O1`, to: `R${room}O2`, weight: 1 });
    edges.push({ from: `R${room}O2`, to: `R${room}O3`, weight: 1 });
  }

  return edges;
}

async function seedMap() {
  try {
    await mongoose.connect(MONGO_URI);
    console.log('Connected to MongoDB');

    const mapId = 'floor1';
    
    // Check if map exists
    const existing = await MapGraph.findOne({ mapId });
    
    const nodes = buildNodes();
    const edges = buildEdges();

    if (existing) {
      // Update existing map
      await MapGraph.updateOne(
        { mapId },
        { $set: { nodes, edges, name: 'Hospital Floor 1', building: 'Main', floor: '1' } }
      );
      console.log(`Updated map: ${mapId} with ${nodes.length} nodes and ${edges.length} edges`);
    } else {
      // Create new map
      await MapGraph.create({
        mapId,
        name: 'Hospital Floor 1',
        building: 'Main',
        floor: '1',
        nodes,
        edges
      });
      console.log(`Created map: ${mapId} with ${nodes.length} nodes and ${edges.length} edges`);
    }

    console.log('Seed complete!');
    process.exit(0);
  } catch (err) {
    console.error('Seed error:', err);
    process.exit(1);
  }
}

seedMap();
