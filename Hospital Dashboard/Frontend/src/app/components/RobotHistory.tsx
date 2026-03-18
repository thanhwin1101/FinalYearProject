import { Package, Clock, MapPin, CheckCircle, XCircle } from 'lucide-react';
import { Badge } from '@/app/components/ui/badge';
import { format } from 'date-fns';

export interface DeliveryHistory {
  id: string;
  timestamp: string;
  robotId: string;
  robotName: string;
  patientId?: string;
  patientName: string;
  destination: string;
  duration?: number;
  status: 'completed' | 'failed' | 'cancelled';
}


interface DeliveryHistoryTableProps {
  history: DeliveryHistory[];
  loading?: boolean;
}

export function DeliveryHistoryTable({ history, loading }: DeliveryHistoryTableProps) {
  const getStatusBadge = (status: string) => {
    switch (status) {
      case 'completed':
        return <Badge className="bg-green-100 text-green-800"><CheckCircle className="w-3 h-3 mr-1" />Completed</Badge>;
      case 'cancelled':
      case 'failed':
        return <Badge className="bg-red-100 text-red-800"><XCircle className="w-3 h-3 mr-1" />{status === 'cancelled' ? 'Cancelled' : 'Failed'}</Badge>;
      default:
        return <Badge className="bg-blue-100 text-blue-800"><Clock className="w-3 h-3 mr-1" />{status}</Badge>;
    }
  };

  const formatDuration = (minutes?: number) => {
    if (!minutes) return '-';
    if (minutes < 60) return `${minutes}m`;
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return `${hours}h ${mins}m`;
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-48 text-gray-500">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-green-600"></div>
      </div>
    );
  }

  return (
    <div className="overflow-auto max-h-[580px]">
      <table className="w-full text-sm">
        <thead className="bg-gray-100 sticky top-0">
          <tr>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Time</th>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Robot</th>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Patient</th>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Destination</th>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Duration</th>
            <th className="px-2 py-2 text-left font-medium text-gray-700">Status</th>
          </tr>
        </thead>
        <tbody>
          {history.length > 0 ? (
            history.map((item) => (
              <tr key={item.id} className="border-t hover:bg-gray-50">
                <td className="px-2 py-2 text-gray-600">
                  <div className="flex flex-col">
                    <span className="font-medium text-xs">{format(new Date(item.timestamp), 'MMM dd')}</span>
                    <span className="text-xs text-gray-500">{format(new Date(item.timestamp), 'HH:mm')}</span>
                  </div>
                </td>
                <td className="px-2 py-2">
                  <span className="font-medium text-gray-900 text-xs">{item.robotName}</span>
                </td>
                <td className="px-2 py-2">
                  <span className="text-gray-900 text-xs">{item.patientName}</span>
                </td>
                <td className="px-2 py-2">
                  <div className="flex items-center gap-1">
                    <MapPin className="w-3 h-3 text-gray-400" />
                    <span className="text-gray-900 text-xs">{item.destination}</span>
                  </div>
                </td>
                <td className="px-2 py-2 text-gray-600 text-xs">
                  {formatDuration(item.duration)}
                </td>
                <td className="px-2 py-2">
                  {getStatusBadge(item.status)}
                </td>
              </tr>
            ))
          ) : (
            <tr>
              <td colSpan={6} className="px-4 py-8 text-center text-gray-500">
                <Package className="w-10 h-10 mx-auto mb-2 text-gray-300" />
                <p className="text-sm">No delivery history</p>
              </td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  );
}
