import { useState } from 'react';
import { Battery, Send, XCircle, User, MapPin, Navigation, History, List } from 'lucide-react';
import { Button } from '@/app/components/ui/button';
import { Badge } from '@/app/components/ui/badge';
import { Robot, RobotStatus } from '@/app/types/robot';
import { Patient } from '@/app/types/patient';
import { DeliveryHistoryTable, BipedHistoryTable, DeliveryHistory, BipedUsageHistory } from './RobotHistory';

interface RobotManagementProps {
  robots: Robot[];
  selectedPatient?: Patient;
  onSendRobot: (patientId: string) => void;
  onCancelTask: (missionId: string) => void;
  activeMission?: { patientId: string; robotId?: string; missionId?: string } | null;
  deliveryHistory?: DeliveryHistory[];
  bipedHistory?: BipedUsageHistory[];
}

type BipedTab = 'status' | 'history';
type CarryTab = 'status' | 'history';

export function RobotManagement({ 
  robots, 
  selectedPatient, 
  onSendRobot, 
  onCancelTask,
  activeMission,
  deliveryHistory = [],
  bipedHistory = []
}: RobotManagementProps) {
  const [bipedTab, setBipedTab] = useState<BipedTab>('status');
  const [carryTab, setCarryTab] = useState<CarryTab>('status');
  
  const bipedRobots = robots.filter(r => r.type === 'Biped');
  const carryRobots = robots.filter(r => r.type === 'Carry');

  const getStatusBadge = (status: RobotStatus) => {
    const isBusy = status !== 'Idle';
    return (
      <Badge className={isBusy ? 'bg-yellow-100 text-yellow-800' : 'bg-green-100 text-green-800'}>
        {isBusy ? 'Busy' : 'Idle'}
      </Badge>
    );
  };

  const getBatteryColor = (level: number) => {
    if (level > 50) return 'text-green-600';
    if (level > 20) return 'text-yellow-600';
    return 'text-red-600';
  };

  const isPatientHasActiveMission = selectedPatient && activeMission?.patientId === selectedPatient.id;

  return (
    <div className="space-y-4 h-full flex flex-col">
      <h2 className="text-xl font-semibold text-gray-900">Robot Fleet Status</h2>

      {/* Top Section - Patient Info & Send Button */}
      <div className="bg-gray-50 border-2 border-dashed border-gray-300 rounded-lg p-4 min-h-[140px]">
        {selectedPatient ? (
          <div className="flex items-center justify-between h-full">
            <div className="flex items-center gap-4">
              <div className="w-16 h-16 bg-blue-100 rounded-full flex items-center justify-center">
                {selectedPatient.photo ? (
                  <img 
                    src={selectedPatient.photo} 
                    alt={selectedPatient.fullName}
                    className="w-16 h-16 rounded-full object-cover"
                  />
                ) : (
                  <User className="w-8 h-8 text-blue-600" />
                )}
              </div>
              <div className="space-y-1">
                <h3 className="text-lg font-semibold text-gray-900">{selectedPatient.fullName}</h3>
                <div className="text-sm text-gray-600">
                  <span className="font-medium">MRN:</span> {selectedPatient.mrn}
                </div>
                <div className="text-sm text-gray-600">
                  <span className="font-medium">Bed:</span> {selectedPatient.roomBedId}
                </div>
                <div className="text-sm text-gray-600">
                  <span className="font-medium">Department:</span> {selectedPatient.department}
                </div>
                <Badge className={
                  selectedPatient.status === 'Critical' ? 'bg-red-100 text-red-800' :
                  selectedPatient.status === 'Stable' ? 'bg-green-100 text-green-800' :
                  selectedPatient.status === 'Recovering' ? 'bg-blue-100 text-blue-800' :
                  'bg-yellow-100 text-yellow-800'
                }>
                  {selectedPatient.status}
                </Badge>
              </div>
            </div>
            <div className="flex flex-col gap-2">
              {isPatientHasActiveMission ? (
                <Button
                  variant="destructive"
                  size="lg"
                  onClick={() => activeMission?.missionId && onCancelTask(activeMission.missionId)}
                  className="px-8"
                >
                  <XCircle className="w-5 h-5 mr-2" />
                  Cancel Mission
                </Button>
              ) : (
                <Button
                  size="lg"
                  onClick={() => onSendRobot(selectedPatient.id)}
                  className="px-8 bg-blue-600 hover:bg-blue-700"
                >
                  <Send className="w-5 h-5 mr-2" />
                  Send Robot
                </Button>
              )}
            </div>
          </div>
        ) : (
          <div className="flex items-center justify-center h-full text-gray-500">
            <div className="text-center">
              <User className="w-12 h-12 mx-auto mb-2 text-gray-400" />
              <p>Select a patient from the floor map to display information</p>
            </div>
          </div>
        )}
      </div>

      {/* Bottom Section - Robot Tables */}
      <div className="flex-1 grid grid-cols-2 gap-4 min-h-[620px]">
        {/* Left - Biped Robot Table */}
        <div className="border rounded-lg overflow-hidden">
          <div className="bg-blue-600 text-white px-4 py-2 font-semibold flex items-center justify-between">
            <div className="flex items-center gap-2">
              <span className="text-xl">🚶</span>
              Biped Robot
            </div>
            <div className="flex gap-1">
              <button
                onClick={() => setBipedTab('status')}
                className={`px-3 py-1 rounded text-sm transition-colors ${
                  bipedTab === 'status' 
                    ? 'bg-white text-blue-600' 
                    : 'bg-blue-500 text-white hover:bg-blue-400'
                }`}
              >
                <List className="w-4 h-4 inline mr-1" />
                Status
              </button>
              <button
                onClick={() => setBipedTab('history')}
                className={`px-3 py-1 rounded text-sm transition-colors ${
                  bipedTab === 'history' 
                    ? 'bg-white text-blue-600' 
                    : 'bg-blue-500 text-white hover:bg-blue-400'
                }`}
              >
                <History className="w-4 h-4 inline mr-1" />
                History
              </button>
            </div>
          </div>
          
          {bipedTab === 'status' ? (
            <div className="overflow-auto max-h-[280px]">
              <table className="w-full text-sm">
                <thead className="bg-gray-100 sticky top-0">
                  <tr>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">No.</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Device ID</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Status</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Current User</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Steps</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Battery</th>
                  </tr>
                </thead>
                <tbody>
                  {bipedRobots.length > 0 ? (
                    bipedRobots.map((robot, index) => (
                      <tr key={robot.id} className="border-t hover:bg-gray-50">
                        <td className="px-2 py-2 text-gray-900">{index + 1}</td>
                        <td className="px-2 py-2 font-medium text-gray-900">{robot.id}</td>
                        <td className="px-2 py-2">{getStatusBadge(robot.status)}</td>
                        <td className="px-2 py-2 text-gray-900">
                          {robot.assignedPatientId || '-'}
                        </td>
                        <td className="px-2 py-2 text-gray-900">
                          {(robot as any).stepCount || 0}
                        </td>
                        <td className="px-2 py-2">
                          <div className="flex items-center gap-1">
                            <Battery className={`w-4 h-4 ${getBatteryColor(robot.batteryLevel)}`} />
                            <span className={getBatteryColor(robot.batteryLevel)}>
                              {robot.batteryLevel}%
                            </span>
                          </div>
                        </td>
                      </tr>
                    ))
                  ) : (
                    <tr>
                      <td colSpan={6} className="px-4 py-8 text-center text-gray-500">
                        No Biped Robot available
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          ) : (
            <BipedHistoryTable history={bipedHistory} />
          )}
        </div>

        {/* Right - Carry Robot Table */}
        <div className="border rounded-lg overflow-hidden">
          <div className="bg-green-600 text-white px-4 py-2 font-semibold flex items-center justify-between">
            <div className="flex items-center gap-2">
              <span className="text-xl">🤖</span>
              Carry Robot
            </div>
            <div className="flex gap-1">
              <button
                onClick={() => setCarryTab('status')}
                className={`px-3 py-1 rounded text-sm transition-colors ${
                  carryTab === 'status' 
                    ? 'bg-white text-green-600' 
                    : 'bg-green-500 text-white hover:bg-green-400'
                }`}
              >
                <List className="w-4 h-4 inline mr-1" />
                Status
              </button>
              <button
                onClick={() => setCarryTab('history')}
                className={`px-3 py-1 rounded text-sm transition-colors ${
                  carryTab === 'history' 
                    ? 'bg-white text-green-600' 
                    : 'bg-green-500 text-white hover:bg-green-400'
                }`}
              >
                <History className="w-4 h-4 inline mr-1" />
                History
              </button>
            </div>
          </div>
          
          {carryTab === 'status' ? (
            <div className="overflow-auto max-h-[280px]">
              <table className="w-full text-sm">
                <thead className="bg-gray-100 sticky top-0">
                  <tr>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">No.</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Device Name</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Status</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Current Location</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Destination</th>
                    <th className="px-2 py-2 text-left font-medium text-gray-700">Battery</th>
                  </tr>
                </thead>
                <tbody>
                  {carryRobots.length > 0 ? (
                    carryRobots.map((robot, index) => (
                      <tr key={robot.id} className="border-t hover:bg-gray-50">
                        <td className="px-2 py-2 text-gray-900">{index + 1}</td>
                        <td className="px-2 py-2 font-medium text-gray-900">{robot.name}</td>
                        <td className="px-2 py-2">{getStatusBadge(robot.status)}</td>
                        <td className="px-2 py-2 text-gray-900">
                          <div className="flex items-center gap-1">
                            <MapPin className="w-3 h-3 text-gray-500" />
                            {robot.currentLocation || '-'}
                          </div>
                        </td>
                        <td className="px-2 py-2 text-gray-900">
                          <div className="flex items-center gap-1">
                            {robot.destination ? (
                              <>
                                <Navigation className="w-3 h-3 text-blue-500" />
                                {robot.destination}
                              </>
                            ) : '-'}
                          </div>
                        </td>
                        <td className="px-2 py-2">
                          <div className="flex items-center gap-1">
                            <Battery className={`w-4 h-4 ${getBatteryColor(robot.batteryLevel)}`} />
                            <span className={getBatteryColor(robot.batteryLevel)}>
                              {robot.batteryLevel}%
                            </span>
                          </div>
                        </td>
                      </tr>
                    ))
                  ) : (
                    <tr>
                      <td colSpan={6} className="px-4 py-8 text-center text-gray-500">
                        No Carry Robot available
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          ) : (
            <DeliveryHistoryTable history={deliveryHistory} />
          )}
        </div>
      </div>
    </div>
  );
}