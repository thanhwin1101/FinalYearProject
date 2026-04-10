import { useCallback, useEffect, useRef, useState } from 'react';
import {
  Activity,
  Battery,
  Crosshair,
  Gauge,
  MapPin,
  Plug,
  Radio,
  Ruler,
  SlidersHorizontal,
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
  mode?: string;
  run?: boolean;
  testDash?: boolean;
  r1?: number;  // relay R1 vision (1=ON)
  r2?: number;  // relay R2 line   (1=ON)
  r3?: number;  // relay R3 nfc    (1=ON)
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

function lineBitsToString(bits: number | undefined): string {
  if (bits === undefined || bits === null) return '—';
  return LINE_LABELS.map((_, i) => ((bits >> i) & 1 ? '1' : '0')).join('');
}

export function RobotTestLab() {
  const env = (import.meta as { env: Record<string, string> }).env;
  const defaultStackId = env.VITE_STACK_ROBOT_ID?.trim() || 'carry-stack-1';
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

  const esRef = useRef<EventSource | null>(null);
  const robotIdRef = useRef(robotId);
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
                <div className="text-xs text-muted-foreground mb-1">Line (bit L→R)</div>
                <div className="font-mono font-medium">{lineBitsToString(dbg?.line)}</div>
                <div className="text-xs text-muted-foreground mt-1">
                  Đi theo line do STM32 điều khiển; bit phản ánh cảm biến line hiện tại.
                </div>
              </div>
              <div className="rounded-lg border bg-card p-3 col-span-2">
                <div className="text-xs text-muted-foreground">Ms quay / phanh (tune)</div>
                <div className="font-mono font-medium">
                  spin {dbg?.spinMs ?? '—'} ms · brake {dbg?.brakeMs ?? '—'} ms
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
