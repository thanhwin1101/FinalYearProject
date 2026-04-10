import { useEffect, useRef, useState } from 'react';
import { Wifi, WifiOff, Battery, BatteryWarning, MapPin, Navigation } from 'lucide-react';
import { API_ENDPOINTS } from '@/app/api/config';

interface RobotLiveState {
  robotId: string;
  status: string;
  batteryLevel: number;
  batteryMv: number | null;
  currentNodeId: string | null;
  destBed: string | null;
  ts: number;
}

// ─── Node positions (SVG viewBox 900 × 630) ────────────────────────────────
// Matches the user's hand-drawn floor plan exactly.
//
//  ┌─ Room 1 ──────────┐  corridor  ┌─ Room 2 ──────────┐
//  │ R1O1  R1M1 ← R1D1 │──H-Top ──▶│ R2D1 → R2M1  R2O1 │
//  │ R1O2  R1M2   R1D2 │            │ R2D2   R2M2  R2O2 │
//  │ R1O3  R1M3        │            │        R2M3  R2O3 │
//  └───────────────────┘   J4       └───────────────────┘
//  ┌─ Room 3 ──────────┐  H-Bot     ┌─ Room 4 ──────────┐
//  │ R3O1  R3M1 ← R3D1 │◀──────────▶│ R4D1 → R4M1  R4O1 │
//  │ R3O2  R3M2   R3D2 │            │ R4D2   R4M2  R4O2 │
//  │ R3O3  R3M3        │            │        R4M3  R4O3 │
//  └───────────────────┘   H-Med    └───────────────────┘
//  ┌─ Medicine/Doctor ─┐  (MED)
// ──────────────────────────────────────────────────────────────────────────

const CX = 450; // corridor centre x

const NODES: Record<string, { x: number; y: number; label: string }> = {
  // ── Corridor ──────────────────────────────────────────
  'H-TOP':  { x: CX,   y: 80,  label: 'H-Top' },
  'J4':     { x: CX,   y: 295, label: 'J4'    },
  'H-BOT':  { x: CX,   y: 390, label: 'H-Bot' },
  'H-MED':  { x: CX,   y: 540, label: 'H-Med' },
  'MED':    { x: 230,  y: 580, label: 'MED'   },

  // ── Room 1 (top-left) ─────────────────────────────────
  'R1D1':   { x: 420,  y: 80,  label: 'R1D1'  },
  'R1D2':   { x: 330,  y: 140, label: 'R1D2'  },
  'R1M1':   { x: 260,  y: 80,  label: 'R1M1'  },
  'R1M2':   { x: 260,  y: 148, label: 'R1M2'  },
  'R1M3':   { x: 260,  y: 216, label: 'R1M3'  },
  'R1O1':   { x: 135,  y: 80,  label: 'R1O1'  },
  'R1O2':   { x: 135,  y: 148, label: 'R1O2'  },
  'R1O3':   { x: 135,  y: 216, label: 'R1O3'  },

  // ── Room 2 (top-right) ────────────────────────────────
  'R2D1':   { x: 480,  y: 80,  label: 'R2D1'  },
  'R2D2':   { x: 570,  y: 140, label: 'R2D2'  },
  'R2M1':   { x: 570,  y: 80,  label: 'R2M1'  },
  'R2M2':   { x: 570,  y: 148, label: 'R2M2'  },
  'R2M3':   { x: 570,  y: 216, label: 'R2M3'  },
  'R2O1':   { x: 700,  y: 80,  label: 'R2O1'  },
  'R2O2':   { x: 700,  y: 148, label: 'R2O2'  },
  'R2O3':   { x: 700,  y: 216, label: 'R2O3'  },

  // ── Room 3 (bottom-left) ──────────────────────────────
  'R3D1':   { x: 420,  y: 390, label: 'R3D1'  },
  'R3D2':   { x: 330,  y: 450, label: 'R3D2'  },
  'R3M1':   { x: 260,  y: 390, label: 'R3M1'  },
  'R3M2':   { x: 260,  y: 450, label: 'R3M2'  },
  'R3M3':   { x: 260,  y: 515, label: 'R3M3'  },
  'R3O1':   { x: 135,  y: 390, label: 'R3O1'  },
  'R3O2':   { x: 135,  y: 450, label: 'R3O2'  },
  'R3O3':   { x: 135,  y: 515, label: 'R3O3'  },

  // ── Room 4 (bottom-right) ─────────────────────────────
  'R4D1':   { x: 480,  y: 390, label: 'R4D1'  },
  'R4D2':   { x: 570,  y: 450, label: 'R4D2'  },
  'R4M1':   { x: 570,  y: 390, label: 'R4M1'  },
  'R4M2':   { x: 570,  y: 450, label: 'R4M2'  },
  'R4M3':   { x: 570,  y: 515, label: 'R4M3'  },
  'R4O1':   { x: 700,  y: 390, label: 'R4O1'  },
  'R4O2':   { x: 700,  y: 450, label: 'R4O2'  },
  'R4O3':   { x: 700,  y: 515, label: 'R4O3'  },
};

// Edges that match the arrows in the original diagram
const EDGES: [string, string][] = [
  // Corridor spine
  ['H-TOP', 'J4'], ['J4', 'H-BOT'], ['H-BOT', 'H-MED'], ['H-MED', 'MED'],
  // Room 1
  ['H-TOP', 'R1D1'], ['R1D1', 'R1D2'], ['R1D1', 'R1M1'],
  ['R1D2', 'R1O1'], ['R1M1', 'R1M2'], ['R1M2', 'R1M3'],
  ['R1O1', 'R1O2'], ['R1O2', 'R1O3'],
  // Room 2
  ['H-TOP', 'R2D1'], ['R2D1', 'R2D2'], ['R2D1', 'R2M1'],
  ['R2D2', 'R2O1'], ['R2M1', 'R2M2'], ['R2M2', 'R2M3'],
  ['R2O1', 'R2O2'], ['R2O2', 'R2O3'],
  // Room 3
  ['H-BOT', 'R3D1'], ['R3D1', 'R3D2'], ['R3D1', 'R3M1'],
  ['R3D2', 'R3O1'], ['R3M1', 'R3M2'], ['R3M2', 'R3M3'],
  ['R3O1', 'R3O2'], ['R3O2', 'R3O3'],
  // Room 4
  ['H-BOT', 'R4D1'], ['R4D1', 'R4D2'], ['R4D1', 'R4M1'],
  ['R4D2', 'R4O1'], ['R4M1', 'R4M2'], ['R4M2', 'R4M3'],
  ['R4O1', 'R4O2'], ['R4O2', 'R4O3'],
];

function batteryColor(pct: number) {
  if (pct > 50) return '#22c55e';
  if (pct > 20) return '#f59e0b';
  return '#ef4444';
}

function statusBadgeCls(status: string) {
  const m: Record<string, string> = {
    idle:        'bg-gray-100 text-gray-700',
    busy:        'bg-blue-100 text-blue-800',
    low_battery: 'bg-red-100 text-red-700',
    follow:      'bg-purple-100 text-purple-800',
    charging:    'bg-green-100 text-green-800',
  };
  return m[status] ?? 'bg-gray-100 text-gray-600';
}

export function RobotLiveTracking() {
  const [robots, setRobots] = useState<Map<string, RobotLiveState>>(new Map());
  const [connected, setConnected] = useState(false);
  const esRef = useRef<EventSource | null>(null);

  useEffect(() => {
    const base = (import.meta as { env: Record<string, string> }).env.VITE_API_URL ?? '';
    const url = `${base}${API_ENDPOINTS.robotsLiveStream}`;

    const connect = () => {
      const es = new EventSource(url);
      esRef.current = es;
      es.onopen = () => setConnected(true);
      es.onmessage = (e) => {
        try {
          const data: RobotLiveState = JSON.parse(e.data);
          setRobots(prev => new Map(prev).set(data.robotId, data));
        } catch { /* skip bad frames */ }
      };
      es.onerror = () => {
        setConnected(false);
        es.close();
        setTimeout(connect, 5000);
      };
    };

    connect();
    return () => { esRef.current?.close(); };
  }, []);

  const robotList = Array.from(robots.values());

  // Normalise a nodeId string so we can look it up in NODES
  const resolveNode = (id: string | null) => {
    if (!id) return null;
    const up = id.trim().toUpperCase();
    // Handle aliases like H_TOP → H-TOP, HTOP → H-TOP
    const normalised = up
      .replace(/^H_?TOP$/,  'H-TOP')
      .replace(/^H_?BOT$/,  'H-BOT')
      .replace(/^H_?MED$/,  'H-MED')
      .replace(/_/g, '-');
    return NODES[normalised] ? { key: normalised, ...NODES[normalised] } : null;
  };

  return (
    <div className="space-y-4">
      {/* Connection banner */}
      <div className={`flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium
        ${connected ? 'bg-green-50 text-green-700' : 'bg-red-50 text-red-700'}`}>
        {connected
          ? <><Wifi className="w-4 h-4" />Live – receiving robot telemetry in real-time</>
          : <><WifiOff className="w-4 h-4" />Disconnected – reconnecting…</>}
      </div>

      {/* Robot status cards */}
      {robotList.length > 0 && (
        <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
          {robotList.map(r => (
            <div key={r.robotId} className="bg-white rounded-xl border shadow-sm p-4 space-y-3">
              <div className="flex items-center justify-between">
                <span className="font-semibold text-gray-800">{r.robotId}</span>
                <span className={`text-xs font-medium px-2 py-0.5 rounded-full ${statusBadgeCls(r.status)}`}>
                  {r.status}
                </span>
              </div>
              <div className="flex items-center gap-4 text-sm text-gray-600">
                <span className="flex items-center gap-1">
                  <MapPin className="w-4 h-4" />
                  {r.currentNodeId ?? '—'}
                </span>
                {r.destBed && (
                  <span className="flex items-center gap-1">
                    <Navigation className="w-4 h-4" />
                    → {r.destBed}
                  </span>
                )}
              </div>
              {/* Battery bar */}
              <div>
                <div className="flex items-center justify-between mb-1">
                  <span className="flex items-center gap-1 text-xs text-gray-500">
                    {r.batteryLevel <= 20
                      ? <BatteryWarning className="w-4 h-4 text-red-500" />
                      : <Battery className="w-4 h-4" />}
                    Battery
                  </span>
                  <span className="text-xs font-medium" style={{ color: batteryColor(r.batteryLevel) }}>
                    {r.batteryLevel}%
                    {r.batteryMv ? ` (${(r.batteryMv / 1000).toFixed(2)} V)` : ''}
                  </span>
                </div>
                <div className="h-2 bg-gray-100 rounded-full overflow-hidden">
                  <div
                    className="h-full rounded-full transition-all duration-500"
                    style={{ width: `${r.batteryLevel}%`, backgroundColor: batteryColor(r.batteryLevel) }}
                  />
                </div>
              </div>
              <div className="text-xs text-gray-400">
                Last update: {new Date(r.ts).toLocaleTimeString('vi-VN')}
              </div>
            </div>
          ))}
        </div>
      )}

      {/* ── SVG Floor Map ─────────────────────────────────────────────────── */}
      <div className="bg-white rounded-xl border shadow-sm p-4">
        <h3 className="text-base font-semibold mb-3 text-gray-800">Floor Map – Live Position</h3>
        <div className="w-full overflow-auto">
          <svg
            viewBox="0 0 900 630"
            className="w-full border rounded-lg bg-[#f8fafc]"
            style={{ minWidth: 420, maxWidth: 900 }}
          >
            <defs>
              <marker id="arr" markerWidth="6" markerHeight="4" refX="5" refY="2" orient="auto">
                <polygon points="0 0, 6 2, 0 4" fill="#94a3b8" />
              </marker>
            </defs>

            {/* ── Room outlines ─────────────────────────────── */}
            {/* Room 1 */}
            <rect x="20"  y="45" width="410" height="230" rx="8"
              fill="#eff6ff" stroke="#93c5fd" strokeWidth="1.5" />
            <text x="120" y="290" fontSize="11" fill="#64748b" fontStyle="italic">Room 1</text>

            {/* Room 2 */}
            <rect x="470" y="45" width="410" height="230" rx="8"
              fill="#f0fdf4" stroke="#86efac" strokeWidth="1.5" />
            <text x="620" y="290" fontSize="11" fill="#64748b" fontStyle="italic">Room 2</text>

            {/* Room 3 */}
            <rect x="20"  y="350" width="410" height="200" rx="8"
              fill="#fff7ed" stroke="#fdba74" strokeWidth="1.5" />
            <text x="120" y="565" fontSize="11" fill="#64748b" fontStyle="italic">Room 3</text>

            {/* Room 4 */}
            <rect x="470" y="350" width="410" height="200" rx="8"
              fill="#fdf4ff" stroke="#d8b4fe" strokeWidth="1.5" />
            <text x="620" y="565" fontSize="11" fill="#64748b" fontStyle="italic">Room 4</text>

            {/* Medicine / Doctor room */}
            <rect x="20" y="555" width="380" height="55" rx="8"
              fill="#fefce8" stroke="#fde68a" strokeWidth="1.5" />
            <text x="55" y="588" fontSize="11" fill="#78716c" fontStyle="italic">Medicine / Doctor room</text>

            {/* Corridor (vertical dashed spine) */}
            <line x1={CX} y1="45" x2={CX} y2="545"
              stroke="#cbd5e1" strokeWidth="10" strokeDasharray="6,4" strokeLinecap="round" />

            {/* ── Static edges ──────────────────────────────── */}
            {EDGES.map(([a, b]) => {
              const na = NODES[a], nb = NODES[b];
              if (!na || !nb) return null;
              return (
                <line key={`${a}-${b}`}
                  x1={na.x} y1={na.y} x2={nb.x} y2={nb.y}
                  stroke="#cbd5e1" strokeWidth="1.2"
                  markerEnd="url(#arr)"
                />
              );
            })}

            {/* ── Static checkpoint nodes ───────────────────── */}
            {Object.entries(NODES).map(([id, pos]) => {
              const isCorr = ['H-TOP','J4','H-BOT','H-MED','MED'].includes(id);
              return (
                <g key={id}>
                  <circle
                    cx={pos.x} cy={pos.y} r={isCorr ? 9 : 7}
                    fill={isCorr ? '#e0f2fe' : '#f1f5f9'}
                    stroke={isCorr ? '#38bdf8' : '#94a3b8'}
                    strokeWidth="1.5"
                  />
                  <text
                    x={pos.x} y={pos.y + (isCorr ? 20 : 18)}
                    textAnchor="middle" fontSize="8" fill="#64748b"
                  >
                    {pos.label}
                  </text>
                </g>
              );
            })}

            {/* ── Live robot markers ────────────────────────── */}
            {robotList.map(r => {
              const cur = resolveNode(r.currentNodeId);
              if (!cur) return null;
              const dst = resolveNode(r.destBed);
              const bc = batteryColor(r.batteryLevel);

              return (
                <g key={r.robotId}>
                  {/* Destination dashed line */}
                  {dst && (
                    <line
                      x1={cur.x} y1={cur.y} x2={dst.x} y2={dst.y}
                      stroke={bc} strokeWidth="1.5" strokeDasharray="5,3"
                      opacity="0.6" markerEnd="url(#arr)"
                    />
                  )}

                  {/* Pulse ring */}
                  <circle cx={cur.x} cy={cur.y} r="14"
                    fill="none" stroke={bc} strokeWidth="2" opacity="0.4">
                    <animate attributeName="r" values="12;22;12" dur="2s" repeatCount="indefinite" />
                    <animate attributeName="opacity" values="0.5;0;0.5" dur="2s" repeatCount="indefinite" />
                  </circle>

                  {/* Robot dot */}
                  <circle cx={cur.x} cy={cur.y} r="10"
                    fill={bc} stroke="white" strokeWidth="2" />
                  <text x={cur.x} y={cur.y + 4}
                    textAnchor="middle" fontSize="9" fill="white" fontWeight="bold">
                    R
                  </text>

                  {/* Label bubble */}
                  <rect
                    x={cur.x - 24} y={cur.y - 28} width="48" height="14"
                    rx="4" fill="white" stroke={bc} strokeWidth="1"
                  />
                  <text x={cur.x} y={cur.y - 18}
                    textAnchor="middle" fontSize="8" fill="#1e293b" fontWeight="600">
                    {r.robotId.replace(/^CARRY-?/i, '#')}
                  </text>
                </g>
              );
            })}
          </svg>
        </div>

        {robotList.length === 0 && (
          <p className="text-center text-sm text-gray-400 mt-3">
            Robot chưa gửi telemetry. Vị trí sẽ xuất hiện ngay khi robot kết nối MQTT.
          </p>
        )}
      </div>
    </div>
  );
}
