import { useCallback, useEffect, useRef, useState } from 'react';
import {
  Activity,
  ArrowDown,
  ArrowDownLeft,
  ArrowDownRight,
  ArrowUp,
  ArrowUpLeft,
  ArrowUpRight,
  Battery,
  Crosshair,
  Gauge,
  MapPin,
  Move,
  Plug,
  Radio,
  RotateCcw,
  RotateCw,
  Ruler,
  Scan,
  SlidersHorizontal,
  Square,
  Terminal,
  Wifi,
  WifiOff,
} from 'lucide-react';
import { API_ENDPOINTS } from '@/app/api/config';
import { getCarryRobotsStatus, sendRobotCommandPayload } from '@/app/api/robots';
import { Button } from '@/app/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/app/components/ui/card';
import { Input } from '@/app/components/ui/input';
import { Label } from '@/app/components/ui/label';
import { Slider } from '@/app/components/ui/slider';
import { Badge } from '@/app/components/ui/badge';

export interface RobotDebugSse {
  battEsp?: number;
  battStm?: number;
  tofMm?: number;
  sr05L?: number;
  sr05R?: number;
  line?: number;
  spinMs?: number;
  brakeMs?: number;
  wallCm?: number;
  runSpeed?: number;
  turnSpeed?: number;
  mode?: string;
  run?: boolean;
  testDash?: boolean;
  r1?: number;  // relay R1 vision (1=ON)
  r2?: number;  // relay R2 line   (1=ON)
  r3?: number;  // relay R3 nfc    (1=ON)
  servoX?: number;
  servoY?: number;
  radar?: boolean;
}

interface SsePayload {
  robotId?: string;
  status?: string;
  batteryLevel?: number | null;
  currentNodeId?: string | null;
  destBed?: string | null;
  ts?: number;
  alertType?: string;
  message?: string;
  debug?: RobotDebugSse;
  stackEvent?: Record<string, unknown>;
  stackLogLine?: string;
  cpRawId?: number;
}

interface StackFeedRow {
  t: number;
  line: string;
  evtLabel?: string;
}

const LINE_LABELS = ['L', 'C', 'R'] as const;

// ── Checkpoint name lookup (cpRawId → tên node) ───────────────────
const CHECKPOINT_NAMES: Record<number, string> = (() => {
  const UID_MAP: Record<string, string> = {
    R1M1: '35:FD:E1:83', R1M2: '45:AB:49:83', R1M3: '35:2E:CA:83',
    R1O1: '45:0E:9D:83', R1O2: '35:58:97:83', R1O3: '35:F0:F8:83',
    R1D1: '35:F6:EF:83', R1D2: '45:C7:37:83',
    R2M1: '35:1A:34:83', R2M2: '45:BF:F6:83', R2M3: '35:DC:8F:83',
    R2O1: '45:35:C3:83', R2O2: '45:27:34:83', R2O3: '35:2A:2D:83',
    R2D1: '35:4C:B8:83', R2D2: '45:81:A4:83',
    R3M1: '35:22:F5:83', R3M2: '45:C2:B8:83', R3M3: '35:BB:B1:83',
    R3O1: '45:26:F3:83', R3O2: '45:1D:A4:83', R3O3: '35:1E:47:83',
    R3D1: '35:45:AF:83', R3D2: '35:35:BA:83',
    R4M1: '45:83:FB:83', R4M2: '45:8E:00:83', R4M3: '35:4D:9B:83',
    R4O1: '45:7D:5A:83', R4O2: '35:DB:EA:83', R4O3: '35:EB:18:83',
    R4D1: '35:48:9F:83', R4D2: '35:26:79:83',
    MED:   '45:54:80:83', J4:    '35:2C:3C:83',
    H_TOP: '45:86:AC:83', H_BOT: '45:79:31:83', H_MED: '45:D3:91:83',
  };
  const m: Record<number, string> = {};
  for (const [name, uid] of Object.entries(UID_MAP)) {
    const parts = uid.split(':').map(x => parseInt(x, 16));
    if (parts.length >= 2) {
      const id = (parts[parts.length - 2] << 8) | parts[parts.length - 1];
      m[id] = name;
    }
  }
  return m;
})();

function lineBitsToString(bits: number | undefined): string {
  if (bits === undefined || bits === null) return '—';
  return LINE_LABELS.map((_, i) => ((bits >> i) & 1 ? '1' : '0')).join('');
}

export function RobotTestLab() {
  const env = (import.meta as { env: Record<string, string> }).env;
  const defaultStackId = env.VITE_STACK_ROBOT_ID?.trim() || 'AGV-01';
  const defaultId =
    env.VITE_ROBOT_TEST_ID?.trim() || defaultStackId;

  const [robotId, setRobotId] = useState(defaultId);
  const [carryIds, setCarryIds] = useState<string[]>([]);
  const [sseOk, setSseOk] = useState(false);
  const [live, setLive] = useState<SsePayload | null>(null);
  const [stackFeed, setStackFeed] = useState<StackFeedRow[]>([]);
  const [log, setLog] = useState<string[]>([]);
  const [pending, setPending] = useState(false);

  const [spinMs, setSpinMs] = useState(400);
  const [brakeMs, setBrakeMs] = useState(150);
  const [wallCm, setWallCm] = useState(55);
  const [runSpeed, setRunSpeed] = useState(190);
  const [turnSpeed, setTurnSpeed] = useState(175);

  const esRef = useRef<EventSource | null>(null);
  const robotIdRef = useRef(robotId);
  const servoXTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [servoXLocal, setServoXLocal] = useState(90);
  const [sweepActive, setSweepActive] = useState(false);
  const [sweepFreqX10, setSweepFreqX10] = useState(5);   // × 0.1 Hz → 0.5 Hz default
  const [sweepAmplitude, setSweepAmplitude] = useState(45); // degrees

  // Per-wheel direct control
  const [wheels, setWheels] = useState({ fl: 0, fr: 0, bl: 0, br: 0 });
  const wheelsRef = useRef({ fl: 0, fr: 0, bl: 0, br: 0 });
  const wheelTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const [radarStep, setRadarStep] = useState(2);
  robotIdRef.current = robotId;

  const pushLog = useCallback((line: string) => {
    setLog(prev => [new Date().toLocaleTimeString() + ' ' + line, ...prev].slice(0, 40));
  }, []);

  const refreshCarryList = useCallback(async () => {
    try {
      const res = await getCarryRobotsStatus();
      const ids = res.robots.map(r => r.robotId);
      setCarryIds(ids);
      if (!robotIdRef.current && ids.length === 1) {
        setRobotId(ids[0]);
      }
    } catch (e) {
      pushLog(`carry/status lỗi: ${e instanceof Error ? e.message : String(e)}`);
    }
  }, [pushLog]);

  useEffect(() => {
    refreshCarryList();
  }, [refreshCarryList]);

  useEffect(() => {
    const base = (import.meta as { env: Record<string, string> }).env.VITE_API_URL ?? '';
    const url = `${base}${API_ENDPOINTS.robotsLiveStream}`;

    const connect = () => {
      const es = new EventSource(url);
      esRef.current = es;
      es.onopen = () => setSseOk(true);
      es.onmessage = (e) => {
        try {
          const data = JSON.parse(e.data) as SsePayload;
          if (!data.robotId || data.robotId !== robotIdRef.current) return;
          setLive(data);
          if (data.stackLogLine) {
            const ev = data.stackEvent;
            const evtLabel =
              ev && typeof ev === 'object' && typeof ev.evt === 'string' ? ev.evt : undefined;
            setStackFeed(prev =>
              [{ t: Date.now(), line: data.stackLogLine!, evtLabel }, ...prev].slice(0, 50)
            );
          }
        } catch {
          /* ignore */
        }
      };
      es.onerror = () => {
        setSseOk(false);
        es.close();
        setTimeout(connect, 4000);
      };
    };

    connect();
    return () => {
      esRef.current?.close();
    };
  }, []);

  const runCmd = async (label: string, body: Record<string, unknown>) => {
    const id = robotId.trim();
    if (!id) {
      pushLog('Chưa nhập Robot ID');
      return;
    }
    setPending(true);
    try {
      const r = await sendRobotCommandPayload(id, body);
      if (r.error) {
        pushLog(`${label}: ${r.error}`);
      } else {
        pushLog(`${label}: ok`);
      }
    } catch (e) {
      pushLog(`${label}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setPending(false);
    }
  };

  const dbg = live?.debug;
  const minBatt =
    dbg?.battEsp !== undefined && dbg?.battStm !== undefined
      ? Math.min(dbg.battEsp, dbg.battStm)
      : (live?.batteryLevel ?? null);

  return (
    <div className="space-y-6 max-w-6xl mx-auto">
      <div>
        <h2 className="text-2xl font-bold text-gray-900 flex items-center gap-2">
          <Crosshair className="w-7 h-7 text-primary" />
          Robot test lab
        </h2>
        <p className="text-gray-600 mt-1">
          Một trang để đổi chế độ Follow / Recovery / Find, bật OLED test, chỉnh ms quay — đồng thời xem
          pin, SR05, ToF và line (qua MQTT → backend → SSE). Cần broker, backend kết nối MQTT, và firmware
          ESP32 gửi trường <code className="text-sm bg-gray-100 px-1 rounded">debug</code> trong telemetry.
        </p>
      </div>

      <div className={`flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium ${
        sseOk ? 'bg-emerald-50 text-emerald-800' : 'bg-amber-50 text-amber-900'
      }`}>
        {sseOk ? <Wifi className="w-4 h-4 shrink-0" /> : <WifiOff className="w-4 h-4 shrink-0" />}
        {sseOk
          ? 'SSE đã kết nối — chờ telemetry từ robot đã chọn'
          : 'SSE chưa kết nối — đang thử lại…'}
      </div>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <Radio className="w-5 h-5" />
            Chọn robot
          </CardTitle>
          <CardDescription>
            ID phải trùng <code className="text-xs bg-muted px-1 rounded">ROBOT_NODE_ID</code> trên ESP32
            và bản ghi trong DB. Có thể đặt mặc định bằng biến{' '}
            <code className="text-xs bg-muted px-1 rounded">VITE_ROBOT_TEST_ID</code> hoặc{' '}
            <code className="text-xs bg-muted px-1 rounded">VITE_STACK_ROBOT_ID</code> (mặc định{' '}
            <code className="text-xs bg-muted px-1 rounded">{defaultStackId}</code>).
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col sm:flex-row gap-3 items-end">
          <div className="flex-1 space-y-2 w-full">
            <Label htmlFor="robot-id">Robot ID</Label>
            <Input
              id="robot-id"
              value={robotId}
              onChange={e => setRobotId(e.target.value.trim())}
              placeholder="VD: CARRY-DEV-01"
            />
          </div>
          <div className="flex flex-wrap gap-2">
            {carryIds.map(id => (
              <Button
                key={id}
                type="button"
                variant={robotId === id ? 'default' : 'outline'}
                size="sm"
                onClick={() => setRobotId(id)}
              >
                {id}
              </Button>
            ))}
            <Button type="button" variant="secondary" size="sm" onClick={refreshCarryList}>
              Làm mới danh sách
            </Button>
            <Button
              type="button"
              variant="outline"
              size="sm"
              onClick={() => setRobotId(defaultStackId)}
            >
              ID stack ({defaultStackId})
            </Button>
          </div>
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2 text-lg">
              <Activity className="w-5 h-5" />
              Trực quan (live)
            </CardTitle>
            <CardDescription>
              Cập nhật theo chu kỳ telemetry robot. Nếu không thấy số, kiểm tra MQTT và ID robot.
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="flex flex-wrap gap-2 items-center">
              <Badge variant="outline">{live?.status ?? '—'}</Badge>
              {dbg?.mode && (
                <Badge className="bg-violet-100 text-violet-900 hover:bg-violet-100">
                  mode: {dbg.mode}
                </Badge>
              )}
              {dbg && (
                <Badge variant={dbg.run ? 'default' : 'secondary'}>
                  {dbg.run ? 'RUN' : 'IDLE'}
                </Badge>
              )}
              {dbg?.testDash !== undefined && (
                <Badge variant={dbg.testDash ? 'destructive' : 'outline'}>
                  OLED test: {dbg.testDash ? 'ON' : 'OFF'}
                </Badge>
              )}
            </div>

            <div className="grid grid-cols-2 gap-3 text-sm">
              <div className="rounded-lg border bg-card p-3 flex items-start gap-2">
                <Battery className="w-4 h-4 mt-0.5 text-muted-foreground" />
                <div>
                  <div className="text-xs text-muted-foreground">Pin (min / từng mạch)</div>
                  <div className="font-mono font-medium">
                    {minBatt !== null && minBatt !== undefined ? `${minBatt}%` : '—'}
                    {dbg?.battEsp !== undefined && dbg?.battStm !== undefined && (
                      <span className="text-muted-foreground font-normal">
                        {' '}(E {dbg.battEsp}% · S {dbg.battStm}%)
                      </span>
                    )}
                  </div>
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 flex items-start gap-2">
                <Ruler className="w-4 h-4 mt-0.5 text-muted-foreground" />
                <div>
                  <div className="text-xs text-muted-foreground">ToF (mm)</div>
                  <div className="font-mono font-medium">{dbg?.tofMm ?? '—'}</div>
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 col-span-2 flex items-start gap-2">
                <Gauge className="w-4 h-4 mt-0.5 text-muted-foreground" />
                <div className="flex-1">
                  <div className="text-xs text-muted-foreground">SR05 trái / phải (cm)</div>
                  <div className="font-mono font-medium">
                    {dbg?.sr05L !== undefined && dbg?.sr05R !== undefined
                      ? `${dbg.sr05L} / ${dbg.sr05R}`
                      : '— / —'}
                    {dbg?.wallCm !== undefined && (
                      <span className="text-muted-foreground font-normal"> · ngưỡng {dbg.wallCm} cm</span>
                    )}
                  </div>
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 col-span-2">
                <div className="text-xs text-muted-foreground mb-2">Cảm biến line (L · C · R)</div>
                <div className="flex items-center gap-4">
                  <div className="flex gap-4">
                    {(['L', 'C', 'R'] as const).map((label, i) => {
                      const on = dbg?.line !== undefined && (((dbg.line >> i) & 1) === 1);
                      return (
                        <div key={label} className="flex flex-col items-center gap-1">
                          <div className={`w-7 h-7 rounded-full border-2 transition-colors ${on ? 'bg-green-500 border-green-600 shadow shadow-green-300' : 'bg-gray-200 border-gray-300'}`} />
                          <span className="text-xs font-medium text-muted-foreground">{label}</span>
                        </div>
                      );
                    })}
                  </div>
                  <span className="text-xs font-mono text-muted-foreground">{lineBitsToString(dbg?.line)}</span>
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 col-span-2">
                <div className="text-xs text-muted-foreground">Ms quay / phanh (tune)</div>
                <div className="font-mono font-medium">
                  spin {dbg?.spinMs ?? '—'} ms · brake {dbg?.brakeMs ?? '—'} ms
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 col-span-2">
                <div className="text-xs text-muted-foreground">Tốc độ đi / quay (tune)</div>
                <div className="font-mono font-medium">
                  run {dbg?.runSpeed ?? '—'} · turn {dbg?.turnSpeed ?? '—'}
                </div>
              </div>
            </div>

            <div className="text-xs text-muted-foreground border-t pt-3">
              Node: <span className="font-mono">{live?.currentNodeId ?? '—'}</span>
              {live?.destBed ? (
                <>
                  {' '}
                  · Đích: <span className="font-mono">{live.destBed}</span>
                </>
              ) : null}
              {live?.ts ? (
                <>
                  {' '}
                  · SSE {new Date(live.ts).toLocaleTimeString()}
                </>
              ) : null}
            </div>

            {live?.alertType && (
              <div className="text-sm rounded-md bg-amber-50 border border-amber-200 px-3 py-2 text-amber-900">
                <strong>{live.alertType}</strong>
                {live.message ? `: ${live.message}` : ''}
              </div>
            )}
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2 text-lg">
              <SlidersHorizontal className="w-5 h-5" />
              Điều khiển
            </CardTitle>
            <CardDescription>
              Gửi qua <code className="text-xs bg-muted px-1 rounded">POST /api/robots/:id/command</code>
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-6">
            <div>
              <Label className="mb-2 block">Chế độ</Label>
              <div className="flex flex-wrap gap-2">
                {(['auto', 'follow', 'recovery', 'find'] as const).map(m => (
                  <Button
                    key={m}
                    type="button"
                    variant="outline"
                    disabled={pending}
                    onClick={() => runCmd(`set_mode ${m}`, { command: 'set_mode', mode: m })}
                  >
                    {m}
                  </Button>
                ))}
              </div>
            </div>

            <div>
              <Label className="mb-2 block">OLED test dashboard</Label>
              <div className="flex gap-2">
                <Button
                  type="button"
                  disabled={pending}
                  onClick={() => runCmd('test_dashboard on', { command: 'test_dashboard', enabled: true })}
                >
                  Bật
                </Button>
                <Button
                  type="button"
                  variant="secondary"
                  disabled={pending}
                  onClick={() => runCmd('test_dashboard off', { command: 'test_dashboard', enabled: false })}
                >
                  Tắt
                </Button>
              </div>
            </div>

            <div>
              <Label className="mb-2 block">Tune (ms / cm) — gửi một lần</Label>
              <div className="space-y-4">
                <div>
                  <div className="flex justify-between text-sm mb-1">
                    <span>Spin ms</span>
                    <span className="font-mono">{spinMs}</span>
                  </div>
                  <Slider
                    min={50}
                    max={2000}
                    step={10}
                    value={[spinMs]}
                    onValueChange={v => setSpinMs(v[0])}
                  />
                </div>
                <div>
                  <div className="flex justify-between text-sm mb-1">
                    <span>Brake ms</span>
                    <span className="font-mono">{brakeMs}</span>
                  </div>
                  <Slider
                    min={0}
                    max={800}
                    step={10}
                    value={[brakeMs]}
                    onValueChange={v => setBrakeMs(v[0])}
                  />
                </div>
                <div>
                  <div className="flex justify-between text-sm mb-1">
                    <span>Wall cm (SR05)</span>
                    <span className="font-mono">{wallCm}</span>
                  </div>
                  <Slider
                    min={10}
                    max={200}
                    step={1}
                    value={[wallCm]}
                    onValueChange={v => setWallCm(v[0])}
                  />
                </div>
                <Button
                  type="button"
                  className="w-full"
                  disabled={pending}
                  onClick={() =>
                    runCmd('tune_turn', {
                      command: 'tune_turn',
                      spinMs,
                      brakeMs,
                      wallCm,
                    })
                  }
                >
                  Gửi tune_turn
                </Button>
              </div>
            </div>

            <div>
              <Label className="mb-2 block">Tốc độ động cơ — gửi một lần</Label>
              <div className="space-y-4">
                <div>
                  <div className="flex justify-between text-sm mb-1">
                    <span>Run speed (đi thẳng)</span>
                    <span className="font-mono">{runSpeed}</span>
                  </div>
                  <Slider
                    min={50}
                    max={255}
                    step={1}
                    value={[runSpeed]}
                    onValueChange={v => setRunSpeed(v[0])}
                  />
                </div>
                <div>
                  <div className="flex justify-between text-sm mb-1">
                    <span>Turn speed (quay 90°/180°)</span>
                    <span className="font-mono">{turnSpeed}</span>
                  </div>
                  <Slider
                    min={50}
                    max={255}
                    step={1}
                    value={[turnSpeed]}
                    onValueChange={v => setTurnSpeed(v[0])}
                  />
                </div>
                <Button
                  type="button"
                  className="w-full"
                  disabled={pending}
                  onClick={() =>
                    runCmd('tune_speed', {
                      command: 'tune_speed',
                      runSpeed,
                      turnSpeed,
                    })
                  }
                >
                  Gửi tune_speed
                </Button>
              </div>
            </div>

            <div>
              <Label className="mb-2 block">Khác</Label>
              <div className="flex flex-wrap gap-2">
                <Button
                  type="button"
                  variant="outline"
                  disabled={pending}
                  onClick={() => runCmd('stop', { command: 'stop' })}
                >
                  stop
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  disabled={pending}
                  onClick={() => runCmd('resume', { command: 'resume' })}
                >
                  resume
                </Button>
              </div>
            </div>
          </CardContent>
        </Card>
      </div>

      {/* ── Directional Navigation ─────────────────────────────────── */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        <Card className="lg:col-span-2">
          <CardHeader>
            <CardTitle className="flex items-center gap-2 text-lg">
              <Move className="w-5 h-5" />
              Điều hướng (Mecanum)
            </CardTitle>
            <CardDescription>
              Gửi <code className="text-xs bg-muted px-1 rounded">CMD_DIRECT_VEL</code> (Vx, Vy, Vr) qua UART
              đến STM32. Tốc độ mặc định ±150.
            </CardDescription>
          </CardHeader>
          <CardContent>
            <div className="flex flex-col items-center gap-2">
              {/* Row 1: diagonal-up-left, forward, diagonal-up-right */}
              <div className="flex gap-2">
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↖ chéo trái', { command: 'direct_vel', vx: -120, vy: 150, vr: 0 })}
                  title="Chéo trái-trước">
                  <ArrowUpLeft className="w-5 h-5" />
                </Button>
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↑ tiến', { command: 'direct_vel', vx: 0, vy: 150, vr: 0 })}
                  title="Đi thẳng">
                  <ArrowUp className="w-5 h-5" />
                </Button>
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↗ chéo phải', { command: 'direct_vel', vx: 120, vy: 150, vr: 0 })}
                  title="Chéo phải-trước">
                  <ArrowUpRight className="w-5 h-5" />
                </Button>
              </div>

              {/* Row 2: rotate-left, STOP, rotate-right */}
              <div className="flex gap-2">
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↺ xoay trái', { command: 'direct_vel', vx: 0, vy: 0, vr: -120 })}
                  title="Xoay trái">
                  <RotateCcw className="w-5 h-5" />
                </Button>
                <Button type="button" variant="destructive" size="icon" disabled={pending}
                  onClick={() => runCmd('■ dừng', { command: 'direct_vel', vx: 0, vy: 0, vr: 0 })}
                  title="Dừng">
                  <Square className="w-5 h-5" />
                </Button>
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↻ xoay phải', { command: 'direct_vel', vx: 0, vy: 0, vr: 120 })}
                  title="Xoay phải">
                  <RotateCw className="w-5 h-5" />
                </Button>
              </div>

              {/* Row 3: diagonal-down-left, backward, diagonal-down-right */}
              <div className="flex gap-2">
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↙ chéo trái-lùi', { command: 'direct_vel', vx: -120, vy: -150, vr: 0 })}
                  title="Chéo trái-lùi">
                  <ArrowDownLeft className="w-5 h-5" />
                </Button>
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↓ lùi', { command: 'direct_vel', vx: 0, vy: -150, vr: 0 })}
                  title="Đi lùi">
                  <ArrowDown className="w-5 h-5" />
                </Button>
                <Button type="button" variant="outline" size="icon" disabled={pending}
                  onClick={() => runCmd('↘ chéo phải-lùi', { command: 'direct_vel', vx: 120, vy: -150, vr: 0 })}
                  title="Chéo phải-lùi">
                  <ArrowDownRight className="w-5 h-5" />
                </Button>
              </div>

              <p className="text-xs text-muted-foreground mt-2">
                Vx = strafe (±), Vy = tiến/lùi (±), Vr = xoay (±). Giá trị -255…+255.
              </p>
            </div>
          </CardContent>
        </Card>

        {/* ── Servo Control ──────────────────────────────────────────── */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2 text-lg">
              <Crosshair className="w-5 h-5" />
              Servo Gimbal
            </CardTitle>
            <CardDescription>
              X = ngang (0-180°), Y = dọc. Hiện tại: X={dbg?.servoX ?? '—'}° Y={dbg?.servoY ?? '—'}°
            </CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* Servo X controls */}
            <div>
              <Label className="mb-2 block">
                Servo X (ngang) — {dbg?.servoX ?? servoXLocal}°
              </Label>
              <Slider
                min={0} max={180} step={1}
                value={[dbg?.servoX ?? servoXLocal]}
                disabled={pending || sweepActive}
                onValueChange={([v]) => {
                  setServoXLocal(v);
                  if (servoXTimerRef.current) clearTimeout(servoXTimerRef.current);
                  servoXTimerRef.current = setTimeout(() => {
                    runCmd(`servoX ${v}°`, { command: 'servo_set', x: v });
                  }, 150);
                }}
                className="mb-3"
              />
              <div className="flex flex-wrap gap-2 mb-3">
                <Button type="button" variant="outline" size="sm" disabled={pending || sweepActive}
                  onClick={() => { setServoXLocal(0); runCmd('servoX 0°', { command: 'servo_set', x: 0 }); }}>
                  0°
                </Button>
                <Button type="button" variant="default" size="sm" disabled={pending || sweepActive}
                  onClick={() => { setServoXLocal(90); runCmd('servoX center', { command: 'servo_center' }); }}>
                  Giữa
                </Button>
                <Button type="button" variant="outline" size="sm" disabled={pending || sweepActive}
                  onClick={() => { setServoXLocal(180); runCmd('servoX 180°', { command: 'servo_set', x: 180 }); }}>
                  180°
                </Button>
              </div>
              {/* Sweep mode: sine wave on STM32 */}
              <div className="border-t pt-3 space-y-3">
                <Label className="text-xs font-medium">
                  Quét Sin (STM32) — tần số: {(sweepFreqX10 / 10).toFixed(1)} Hz &nbsp;
                  <span className="text-muted-foreground">≈ {(1 / (sweepFreqX10 / 10)).toFixed(1)}s/chu kỳ</span>
                </Label>
                <div className="space-y-1">
                  <span className="text-xs text-muted-foreground">Tần số ({(sweepFreqX10 / 10).toFixed(1)} Hz)</span>
                  <Slider
                    min={1} max={30} step={1}
                    value={[sweepFreqX10]}
                    onValueChange={([v]) => setSweepFreqX10(v)}
                  />
                  <div className="flex justify-between text-xs text-muted-foreground">
                    <span>Chậm (0.1 Hz)</span><span>Nhanh (3.0 Hz)</span>
                  </div>
                </div>
                <div className="space-y-1">
                  <span className="text-xs text-muted-foreground">Biên độ (±{sweepAmplitude}°, quét {90 - sweepAmplitude}°→{90 + sweepAmplitude}°)</span>
                  <Slider
                    min={10} max={85} step={5}
                    value={[sweepAmplitude]}
                    onValueChange={([v]) => setSweepAmplitude(v)}
                  />
                  <div className="flex justify-between text-xs text-muted-foreground">
                    <span>Hẹp (±10°)</span><span>Rộng (±85°)</span>
                  </div>
                </div>
                <Button type="button" size="sm"
                  variant={sweepActive ? 'destructive' : 'default'}
                  disabled={pending}
                  onClick={() => {
                    const next = !sweepActive;
                    setSweepActive(next);
                    runCmd(next ? `sweep sin start f=${sweepFreqX10 / 10}Hz amp=±${sweepAmplitude}°` : 'sweep stop',
                      { command: 'servo_sweep', start: next, center: 90, amplitude: sweepAmplitude, freqX10: sweepFreqX10 });
                  }}>
                  {sweepActive ? '⏹ Dừng quét' : '▶ Bắt đầu quét Sin'}
                </Button>
              </div>
            </div>

            {/* Servo Y controls */}
            <div>
              <Label className="mb-2 block">Servo Y (dọc)</Label>
              <div className="flex flex-wrap gap-2">
                <Button type="button" variant="outline" size="sm" disabled={pending}
                  onClick={() => runCmd('servoY 45° (cúi)', { command: 'servo_set', y: 45 })}>
                  45° cúi
                </Button>
                <Button type="button" variant="outline" size="sm" disabled={pending}
                  onClick={() => runCmd('servoY -10', { command: 'servo_set', y: Math.max(0, (dbg?.servoY ?? 90) - 10) })}>
                  −10
                </Button>
                <Button type="button" variant="default" size="sm" disabled={pending}
                  onClick={() => runCmd('servoY center', { command: 'servo_center' })}>
                  Giữa
                </Button>
                <Button type="button" variant="outline" size="sm" disabled={pending}
                  onClick={() => runCmd('servoY +10', { command: 'servo_set', y: Math.min(180, (dbg?.servoY ?? 90) + 10) })}>
                  +10
                </Button>
                <Button type="button" variant="outline" size="sm" disabled={pending}
                  onClick={() => runCmd('servoY 115° (ngửa)', { command: 'servo_set', y: 115 })}>
                  115° ngửa
                </Button>
              </div>
            </div>

            {/* Radar Scan */}
            <div className="border-t pt-4">
              <Label className="mb-2 block flex items-center gap-2">
                <Scan className="w-4 h-4" />
                Radar Scan
              </Label>
              <p className="text-xs text-muted-foreground mb-2">
                Servo X quét 0°→180° như radar. Khi HuskyLens (line mode) phát hiện line → dừng quét → tiến đến line.
              </p>
              <div className="flex gap-2 items-center">
                <Button type="button"
                  variant={dbg?.radar ? 'destructive' : 'default'}
                  disabled={pending}
                  onClick={() => runCmd(
                    dbg?.radar ? 'radar_scan stop' : 'radar_scan start',
                    { command: 'radar_scan', start: !dbg?.radar }
                  )}>
                  {dbg?.radar ? 'Dừng Radar' : 'Bắt đầu Radar'}
                </Button>
                {dbg?.radar && (
                  <Badge variant="destructive" className="animate-pulse">
                    Đang quét…
                  </Badge>
                )}
              </div>
              {/* Radar sweep speed */}
              <div className="mt-3">
                <Label className="mb-1 block text-xs">
                  Tốc độ quét — {radarStep}°/bước
                </Label>
                <Slider
                  min={1} max={15} step={1}
                  value={[radarStep]}
                  onValueChange={([v]) => {
                    setRadarStep(v);
                    runCmd(`radar_speed ${v}°`, { command: 'radar_speed', step: v });
                  }}
                />
                <div className="flex justify-between text-xs text-muted-foreground mt-1">
                  <span>Chậm (1°)</span><span>Nhanh (15°)</span>
                </div>
              </div>
            </div>
          </CardContent>
        </Card>
      </div>

      {/* ── Per-wheel control ─────────────────────────────────────────── */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <SlidersHorizontal className="w-5 h-5" />
            Điều khiển từng bánh xe
          </CardTitle>
          <CardDescription>
            Gửi <code className="text-xs bg-muted px-1 rounded">CMD_WHEEL_SET</code> – PWM trực tiếp mỗi bánh (-200…+200).
            Dùng để kiểm tra bánh bị hỏng (BL = Back-Left, L298N #2 ENA=PB14).
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-2 gap-x-8 gap-y-4">
            {(['fl', 'fr', 'bl', 'br'] as const).map((w) => {
              const labels: Record<string, string> = { fl: 'FL – Trước Trái', fr: 'FR – Trước Phải', bl: 'BL – Sau Trái', br: 'BR – Sau Phải' };
              const val = wheels[w];
              return (
                <div key={w}>
                  <Label className="mb-1 block text-sm font-medium">
                    {labels[w]} — <span className={val > 0 ? 'text-green-600' : val < 0 ? 'text-red-500' : 'text-muted-foreground'}>{val > 0 ? `+${val}` : val}</span>
                  </Label>
                  <Slider
                    min={-200} max={200} step={10}
                    value={[val]}
                    disabled={pending}
                    onValueChange={([v]) => {
                      const next = { ...wheelsRef.current, [w]: v };
                      wheelsRef.current = next;
                      setWheels(next);
                      if (wheelTimerRef.current) clearTimeout(wheelTimerRef.current);
                      wheelTimerRef.current = setTimeout(() => {
                        const wh = wheelsRef.current;
                        runCmd(`wheel fl=${wh.fl} fr=${wh.fr} bl=${wh.bl} br=${wh.br}`, { command: 'wheel_set', ...wh });
                      }, 100);
                    }}
                  />
                </div>
              );
            })}
          </div>
          <div className="flex gap-2 mt-4">
            <Button type="button" variant="destructive" size="sm" disabled={pending}
              onClick={() => {
                const zero = { fl: 0, fr: 0, bl: 0, br: 0 };
                wheelsRef.current = zero;
                setWheels(zero);
                runCmd('wheel stop', { command: 'wheel_set', fl: 0, fr: 0, bl: 0, br: 0 });
              }}>
              Dừng tất cả
            </Button>
            <Button type="button" variant="outline" size="sm" disabled={pending}
              onClick={() => {
                const wh = wheelsRef.current;
                runCmd(`wheel fl=${wh.fl} fr=${wh.fr} bl=${wh.bl} br=${wh.br}`, { command: 'wheel_set', ...wh });
              }}>
              Gửi lại
            </Button>
          </div>
          <p className="text-xs text-muted-foreground mt-2">
            Giá trị dương = tiến, âm = lùi. Watchdog 400ms → tự dừng nếu không gửi lệnh mới.
          </p>
        </CardContent>
      </Card>

      {/* ── NFC / Checkpoint ──────────────────────────────────────────── */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <Radio className="w-5 h-5" />
            NFC / Checkpoint (live)
          </CardTitle>
          <CardDescription>
            Hiển thị checkpoint vừa quét được từ RFID. Dữ liệu cập nhật theo SSE telemetry.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
            <div className="rounded-lg border bg-card p-4 sm:col-span-2">
              <div className="text-xs text-muted-foreground mb-1">Checkpoint hiện tại</div>
              <div className="text-2xl font-mono font-bold text-primary">
                {live?.cpRawId != null
                  ? (CHECKPOINT_NAMES[live.cpRawId] ?? `#${live.cpRawId}`)
                  : '—'}
              </div>
              {live?.cpRawId != null && CHECKPOINT_NAMES[live.cpRawId] == null && (
                <div className="text-xs text-amber-600 mt-1">ID không có trong bảng — kiểm tra checkpointsF1.js</div>
              )}
            </div>
            <div className="rounded-lg border bg-card p-4">
              <div className="text-xs text-muted-foreground mb-1">Raw ID</div>
              <div className="font-mono font-medium text-lg">
                {live?.cpRawId != null ? `0x${live.cpRawId.toString(16).toUpperCase().padStart(4, '0')}` : '—'}
              </div>
              <div className="text-xs text-muted-foreground mt-1">
                decimal: {live?.cpRawId ?? '—'}
              </div>
            </div>
          </div>
          <div className="mt-3 text-xs text-muted-foreground">
            Luồng checkpoint gần đây xem trong mục "Carry stack" phía dưới.
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <MapPin className="w-5 h-5" />
            Carry stack — route test MED → R4M3
          </CardTitle>
          <CardDescription>
            Backend publish lên MQTT <code className="text-xs bg-muted px-1 rounded">carry/robot/cmd</code>;
            robot gửi sự kiện qua <code className="text-xs bg-muted px-1 rounded">carry/robot/evt</code>.
            Chọn Robot ID trùng <code className="text-xs bg-muted px-1 rounded">{defaultStackId}</code> để
            xem checkpoint realtime (SSE).
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 text-sm">
            <div className="rounded-lg border bg-card p-3">
              <div className="text-xs text-muted-foreground">Checkpoint (tên)</div>
              <div className="font-mono font-medium text-base mt-1">
                {live?.currentNodeId ?? '—'}
              </div>
            </div>
            <div className="rounded-lg border bg-card p-3">
              <div className="text-xs text-muted-foreground">ID thô (UART / evt)</div>
              <div className="font-mono font-medium text-base mt-1">
                {live?.cpRawId != null ? live.cpRawId : '—'}
              </div>
            </div>
          </div>

          <div>
            <Label className="mb-2 block">Lệnh stack (qua backend)</Label>
            <div className="flex flex-wrap gap-2">
              <Button
                type="button"
                variant="default"
                disabled={pending}
                onClick={() => runCmd('stack_route_test', { command: 'stack_route_test' })}
              >
                Gửi route MED→R4M3 (pending)
              </Button>
              <Button
                type="button"
                variant="secondary"
                disabled={pending}
                onClick={() => runCmd('stack_route_now', { command: 'stack_route_now' })}
              >
                Route + chạy ngay
              </Button>
              <Button
                type="button"
                variant="outline"
                disabled={pending}
                onClick={() => runCmd('stack_start', { command: 'stack_start' })}
              >
                Start
              </Button>
              <Button
                type="button"
                variant="outline"
                disabled={pending}
                onClick={() => runCmd('stack_cancel', { command: 'stack_cancel' })}
              >
                Cancel
              </Button>
              <Button
                type="button"
                variant="ghost"
                size="sm"
                disabled={pending}
                onClick={() => runCmd('stack_status', { command: 'stack_status' })}
              >
                Status
              </Button>
            </div>
          </div>

          <div>
            <div className="text-xs text-muted-foreground mb-1">Luồng sự kiện checkpoint (realtime)</div>
            <pre className="text-xs font-mono bg-muted/50 rounded-lg p-3 max-h-56 overflow-y-auto whitespace-pre-wrap">
              {stackFeed.length
                ? stackFeed
                    .map(
                      r =>
                        `${new Date(r.t).toLocaleTimeString()}${r.evtLabel ? ` [${r.evtLabel}]` : ''} ${r.line}`
                    )
                    .join('\n')
                : 'Chưa có sự kiện từ stack (kiểm tra MQTT, broker, và Robot ID).'}
            </pre>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <Plug className="w-5 h-5" />
            Kiểm tra 3 relay (độc lập)
          </CardTitle>
          <CardDescription>
            Điều khiển từng nhánh nguồn qua MQTT <code className="text-xs bg-muted px-1 rounded">carry/robot/cmd</code>.
            Robot ID phải là{' '}
            <code className="text-xs bg-muted px-1 rounded">{defaultStackId}</code>. Xác nhận trong luồng sự kiện
            phía trên (relay_ack). Sau khi thử, bấm <strong>Khôi phục auto</strong> để relay theo chế độ
            AUTO/FOLLOW/RECOVERY.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          {(
            [
              { id: 'vision' as const, label: 'R1 — HuskyLens + 2 servo + 2× SR05', dbgKey: 'r1' as const },
              { id: 'line' as const, label: 'R2 — Dò line (STM32)', dbgKey: 'r2' as const },
              { id: 'nfc' as const, label: 'R3 — PN532', dbgKey: 'r3' as const },
            ]
          ).map(row => {
            const isOn = dbg?.[row.dbgKey] === 1;
            return (
            <div
              key={row.id}
              className="flex flex-wrap items-center gap-2 justify-between rounded-lg border bg-card p-3"
            >
              <div className="flex items-center gap-2">
                <span
                  title={isOn ? 'ON' : (dbg ? 'OFF' : '—')}
                  className={`w-2.5 h-2.5 rounded-full shrink-0 ${isOn ? 'bg-green-500' : dbg ? 'bg-gray-400' : 'bg-gray-200'}`}
                />
                <span className="text-sm">{row.label}</span>
              </div>
              <div className="flex gap-2 shrink-0">
                <Button
                  type="button"
                  size="sm"
                  disabled={pending}
                  onClick={() =>
                    runCmd(`relay ${row.id} ON`, {
                      command: 'relay_set',
                      which: row.id,
                      on: true,
                    })
                  }
                >
                  Bật
                </Button>
                <Button
                  type="button"
                  size="sm"
                  variant="outline"
                  disabled={pending}
                  onClick={() =>
                    runCmd(`relay ${row.id} OFF`, {
                      command: 'relay_set',
                      which: row.id,
                      on: false,
                    })
                  }
                >
                  Tắt
                </Button>
              </div>
            </div>
            );
          })}
          <Button
            type="button"
            variant="secondary"
            className="w-full"
            disabled={pending}
            onClick={() => runCmd('relay_resume', { command: 'relay_resume' })}
          >
            Khôi phục điều khiển relay tự động
          </Button>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-lg">
            <Terminal className="w-5 h-5" />
            Nhật ký lệnh
          </CardTitle>
        </CardHeader>
        <CardContent>
          <pre className="text-xs font-mono bg-muted/50 rounded-lg p-3 max-h-48 overflow-y-auto whitespace-pre-wrap">
            {log.length ? log.join('\n') : 'Chưa có lệnh.'}
          </pre>
        </CardContent>
      </Card>
    </div>
  );
}
