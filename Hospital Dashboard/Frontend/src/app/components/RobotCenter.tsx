import { useState, useEffect, useCallback } from 'react';
import { BedMap } from '@/app/components/BedMap';
import { RobotManagement } from '@/app/components/RobotManagement';
import { Patient } from '@/app/types/patient';
import { Robot } from '@/app/types/robot';
import { DeliveryHistory } from '@/app/components/RobotHistory';
import { getDeliveryHistory, getMissions, TransportMission } from '@/app/api/missions';

interface RobotCenterProps {
  patients: Patient[];
  robots: Robot[];
  onBedClick?: (bedId: string, patient?: Patient) => void;
  onCancelTask: (missionId: string) => void;
  onSendRobot?: (patientId: string) => Promise<{ missionId: string; robotId: string } | null>;
}

export function RobotCenter({ patients, robots, onBedClick, onCancelTask, onSendRobot }: RobotCenterProps) {
  const [selectedPatient, setSelectedPatient] = useState<Patient | undefined>(undefined);
  const [selectedBedId, setSelectedBedId] = useState<string | null>(null);
  const [activeMission, setActiveMission] = useState<{ patientId: string; robotId?: string; missionId?: string } | null>(null);
  const [deliveryHistory, setDeliveryHistory] = useState<DeliveryHistory[]>([]);

  const fetchActiveMissions = useCallback(async () => {
    try {

      const missions = await getMissions({ status: 'pending,en_route,arrived' });
      if (missions.length > 0) {
        const mission = missions[0];

        const patient = patients.find(p => p.roomBedId === mission.bedId);
        if (patient) {
          setActiveMission({
            patientId: patient.id,
            robotId: mission.carryRobotId,
            missionId: mission.missionId
          });
        }
      } else {
        setActiveMission(null);
      }
    } catch (error) {
      console.error('Failed to fetch active missions:', error);
    }
  }, [patients]);

  useEffect(() => {
    const fetchHistory = async () => {
      try {

        const deliveryRes = await getDeliveryHistory({ limit: 50 });
        setDeliveryHistory(deliveryRes.history.map(item => ({
          id: item._id,
          timestamp: item.completedAt || item.createdAt,
          robotId: item.robotId,
          robotName: item.robotName,
          patientId: item.patientId,
          patientName: item.patientName,
          destination: item.destinationRoom,
          duration: item.duration,
          status: item.status as 'completed' | 'failed' | 'cancelled'
        })));

        await fetchActiveMissions();
      } catch (error) {
        console.error('Failed to fetch history:', error);
      }
    };

    fetchHistory();

    const interval = setInterval(fetchHistory, 5000);
    return () => clearInterval(interval);
  }, [fetchActiveMissions]);

  const handleBedClick = (bedId: string, patient?: Patient) => {
    if (selectedBedId === bedId) {

      setSelectedBedId(null);
      setSelectedPatient(undefined);
    } else {
      setSelectedBedId(bedId);
      setSelectedPatient(patient);
    }
    onBedClick?.(bedId, patient);
  };

  const handleSendRobot = async (patientId: string) => {
    const result = await onSendRobot?.(patientId);
    if (result) {

      setActiveMission({
        patientId,
        robotId: result.robotId,
        missionId: result.missionId
      });
    }
  };

  const handleCancelTask = (missionId: string) => {
    setActiveMission(null);
    onCancelTask(missionId);
  };

  return (
    <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
      {}
      <div className="bg-white p-6 rounded-lg shadow-sm border">
        <h2 className="text-xl font-semibold mb-4 text-gray-900">Hospital Floor Map</h2>
        <BedMap
          patients={patients}
          robots={robots}
          onBedClick={handleBedClick}
          selectedBedId={selectedBedId}
        />
      </div>

      {}
      <div className="bg-white p-6 rounded-lg shadow-sm border">
        <RobotManagement
          robots={robots}
          selectedPatient={selectedPatient}
          onSendRobot={handleSendRobot}
          onCancelTask={handleCancelTask}
          activeMission={activeMission}
          deliveryHistory={deliveryHistory}
        />
      </div>
    </div>
  );
}
