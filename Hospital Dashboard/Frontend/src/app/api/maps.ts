import { API_ENDPOINTS } from './config';
import { get, post, put } from './http';

// Map node types
export type NodeKind = 'checkpoint' | 'bed' | 'station' | 'room' | 'corridor';

export interface MapNode {
  nodeId: string;
  kind: NodeKind;
  label?: string;
  coordinates: {
    x: number;
    y: number;
  };
  bedId?: string;
  stationId?: string;
  rfidUid?: string;
  room?: number;
}

export interface MapEdge {
  from: string;
  to: string;
  weight?: number;
  bidirectional?: boolean;
}

export interface MapGraph {
  _id: string;
  mapId: string;
  name?: string;
  nodes: MapNode[];
  edges: MapEdge[];
  createdAt?: string;
  updatedAt?: string;
}

export interface PathResult {
  distance: number | null;
  path: string[];
}

export interface RouteRequest {
  fromX?: number;
  fromY?: number;
  fromNodeId?: string;
  toNodeId?: string;
  bedId?: string;
  stationId?: string;
  rfidUid?: string;
}

// Get map by ID
export async function getMapById(mapId: string = 'F1'): Promise<MapGraph> {
  return get<MapGraph>(`${API_ENDPOINTS.maps}/${mapId}`);
}

// Get all maps
export async function getAllMaps(): Promise<MapGraph[]> {
  return get<MapGraph[]>(API_ENDPOINTS.maps);
}

// Create or update map
export async function upsertMap(mapId: string, data: Partial<MapGraph>): Promise<MapGraph> {
  return put<MapGraph>(`${API_ENDPOINTS.maps}/${mapId}`, data);
}

// Calculate route
export async function calculateRoute(mapId: string, request: RouteRequest): Promise<PathResult> {
  return post<PathResult>(`${API_ENDPOINTS.maps}/${mapId}/route`, request);
}

// Get nearest node to coordinates
export async function getNearestNode(
  mapId: string, 
  x: number, 
  y: number, 
  kind?: NodeKind
): Promise<MapNode | null> {
  return get<MapNode | null>(`${API_ENDPOINTS.maps}/${mapId}/nearest`, {
    params: { x, y, kind: kind || '' }
  });
}

// Add node to map
export async function addNode(mapId: string, node: MapNode): Promise<MapGraph> {
  return post<MapGraph>(`${API_ENDPOINTS.maps}/${mapId}/nodes`, node);
}

// Add edge to map
export async function addEdge(mapId: string, edge: MapEdge): Promise<MapGraph> {
  return post<MapGraph>(`${API_ENDPOINTS.maps}/${mapId}/edges`, edge);
}

// Get all beds from map
export async function getBedsFromMap(mapId: string = 'F1'): Promise<MapNode[]> {
  const map = await getMapById(mapId);
  return map.nodes.filter(n => n.kind === 'bed');
}

// Get all charging stations from map
export async function getStationsFromMap(mapId: string = 'F1'): Promise<MapNode[]> {
  const map = await getMapById(mapId);
  return map.nodes.filter(n => n.kind === 'station');
}
