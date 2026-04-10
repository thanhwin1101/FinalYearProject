import { useState, useEffect, useCallback } from 'react';
import { Search, RefreshCw, Clock, CheckCircle2, XCircle, AlertCircle, Truck } from 'lucide-react';
import { Button } from '@/app/components/ui/button';
import { Input } from '@/app/components/ui/input';
import { Badge } from '@/app/components/ui/badge';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/app/components/ui/table';
import { getDeliveryHistory, DeliveryHistoryItem } from '@/app/api/missions';

const STATUS_MAP: Record<string, { label: string; color: string; icon: React.ReactNode }> = {
  completed:  { label: 'Completed',  color: 'bg-green-100 text-green-800',  icon: <CheckCircle2 className="w-3 h-3 mr-1" /> },
  cancelled:  { label: 'Cancelled',  color: 'bg-gray-100 text-gray-700',    icon: <XCircle className="w-3 h-3 mr-1" /> },
  failed:     { label: 'Failed',     color: 'bg-red-100 text-red-700',      icon: <AlertCircle className="w-3 h-3 mr-1" /> },
  en_route:   { label: 'En Route',   color: 'bg-blue-100 text-blue-800',    icon: <Truck className="w-3 h-3 mr-1" /> },
  arrived:    { label: 'Arrived',    color: 'bg-purple-100 text-purple-800',icon: <CheckCircle2 className="w-3 h-3 mr-1" /> },
  pending:    { label: 'Pending',    color: 'bg-yellow-100 text-yellow-800',icon: <Clock className="w-3 h-3 mr-1" /> },
};

function formatDuration(seconds?: number): string {
  if (!seconds) return '—';
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return m > 0 ? `${m}m ${s}s` : `${s}s`;
}

function formatTime(iso?: string): string {
  if (!iso) return '—';
  const d = new Date(iso);
  return d.toLocaleString('vi-VN', { hour: '2-digit', minute: '2-digit', day: '2-digit', month: '2-digit', year: 'numeric' });
}

const PAGE_SIZE = 15;

export function MissionHistoryTab() {
  const [items, setItems] = useState<DeliveryHistoryItem[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [loading, setLoading] = useState(false);
  const [search, setSearch] = useState('');
  const [statusFilter, setStatusFilter] = useState('');

  const load = useCallback(async (p: number) => {
    setLoading(true);
    try {
      const res = await getDeliveryHistory({
        page: p,
        limit: PAGE_SIZE,
        status: statusFilter || undefined,
      });
      setItems(res.history);
      setTotal(res.pagination.total);
    } catch (err) {
      console.error('Mission history load error:', err);
    } finally {
      setLoading(false);
    }
  }, [statusFilter]);

  useEffect(() => {
    setPage(1);
    load(1);
  }, [load]);

  const filtered = items.filter(item => {
    if (!search) return true;
    const q = search.toLowerCase();
    return (
      item.patientName?.toLowerCase().includes(q) ||
      item.missionId?.toLowerCase().includes(q) ||
      item.robotId?.toLowerCase().includes(q) ||
      item.destinationBed?.toLowerCase().includes(q)
    );
  });

  const totalPages = Math.ceil(total / PAGE_SIZE);

  const statusInfo = (s: string) =>
    STATUS_MAP[s] ?? { label: s, color: 'bg-gray-100 text-gray-600', icon: null };

  return (
    <div className="space-y-4">
      {/* Toolbar */}
      <div className="flex flex-wrap gap-3 items-center">
        <div className="relative flex-1 min-w-48">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
          <Input
            value={search}
            onChange={e => setSearch(e.target.value)}
            placeholder="Search patient, mission ID, robot…"
            className="pl-9 h-10"
          />
        </div>

        <select
          value={statusFilter}
          onChange={e => setStatusFilter(e.target.value)}
          className="h-10 px-3 border border-input rounded-md bg-background text-sm"
        >
          <option value="">All statuses</option>
          {Object.entries(STATUS_MAP).map(([k, v]) => (
            <option key={k} value={k}>{v.label}</option>
          ))}
        </select>

        <Button
          variant="outline"
          size="sm"
          onClick={() => load(page)}
          disabled={loading}
          className="h-10"
        >
          <RefreshCw className={`w-4 h-4 mr-2 ${loading ? 'animate-spin' : ''}`} />
          Refresh
        </Button>
      </div>

      {/* Stats row */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
        {(['completed','en_route','cancelled','failed'] as const).map(s => {
          const count = items.filter(i => i.status === s).length;
          const info = statusInfo(s);
          return (
            <div key={s} className="bg-white rounded-lg border p-3 flex items-center gap-3">
              <div className={`w-8 h-8 rounded-full flex items-center justify-center ${info.color}`}>
                {info.icon}
              </div>
              <div>
                <div className="text-xl font-bold">{count}</div>
                <div className="text-xs text-gray-500">{info.label}</div>
              </div>
            </div>
          );
        })}
      </div>

      {/* Table */}
      <div className="bg-white rounded-xl border shadow-sm overflow-hidden">
        <div className="px-4 py-3 border-b flex items-center justify-between">
          <h3 className="font-semibold text-gray-800">
            Mission History ({total} records)
          </h3>
          {loading && <RefreshCw className="w-4 h-4 animate-spin text-gray-400" />}
        </div>

        <div className="overflow-x-auto">
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead className="w-[120px]">Mission ID</TableHead>
                <TableHead>Patient</TableHead>
                <TableHead>Bed</TableHead>
                <TableHead>Robot</TableHead>
                <TableHead>Status</TableHead>
                <TableHead>Started</TableHead>
                <TableHead>Completed</TableHead>
                <TableHead>Duration</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {filtered.length === 0 ? (
                <TableRow>
                  <TableCell colSpan={8} className="text-center py-10 text-gray-500">
                    {loading ? 'Loading…' : 'No missions found.'}
                  </TableCell>
                </TableRow>
              ) : (
                filtered.map(item => {
                  const info = statusInfo(item.status);
                  return (
                    <TableRow key={item._id} className="hover:bg-gray-50">
                      <TableCell className="font-mono text-xs text-gray-600">
                        {item.missionId?.slice(-8).toUpperCase()}
                      </TableCell>
                      <TableCell className="font-medium">{item.patientName || '—'}</TableCell>
                      <TableCell>
                        <Badge variant="outline" className="text-xs">{item.destinationBed || item.destinationRoom || '—'}</Badge>
                      </TableCell>
                      <TableCell className="text-sm text-gray-600">{item.robotId}</TableCell>
                      <TableCell>
                        <span className={`inline-flex items-center text-xs font-medium px-2 py-0.5 rounded-full ${info.color}`}>
                          {info.icon}{info.label}
                        </span>
                      </TableCell>
                      <TableCell className="text-sm text-gray-600">{formatTime(item.startedAt || item.createdAt)}</TableCell>
                      <TableCell className="text-sm text-gray-600">{formatTime(item.completedAt || item.returnedAt)}</TableCell>
                      <TableCell className="text-sm">{formatDuration(item.duration)}</TableCell>
                    </TableRow>
                  );
                })
              )}
            </TableBody>
          </Table>
        </div>

        {/* Pagination */}
        {totalPages > 1 && (
          <div className="px-4 py-3 border-t flex items-center justify-between">
            <span className="text-sm text-gray-500">
              Page {page} of {totalPages}
            </span>
            <div className="flex gap-2">
              <Button variant="outline" size="sm" disabled={page <= 1} onClick={() => { setPage(p => p - 1); load(page - 1); }}>
                Previous
              </Button>
              <Button variant="outline" size="sm" disabled={page >= totalPages} onClick={() => { setPage(p => p + 1); load(page + 1); }}>
                Next
              </Button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
