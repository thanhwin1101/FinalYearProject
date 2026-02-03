import { useState, useEffect, useCallback } from 'react';
import { BedMap } from '@/app/components/BedMap';
import { RobotManagement } from '@/app/components/RobotManagement';
import { Patient } from '@/app/types/patient';
import { Robot } from '@/app/types/robot';
import { DeliveryHistory, BipedUsageHistory } from '@/app/components/RobotHistory';
import { getDeliveryHistory, getMissions, TransportMission } from '@/app/api/missions';
import { getBipedSessionHistory } from '@/app/api/robots';

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
  const [bipedHistory, setBipedHistory] = useState<BipedUsageHistory[]>([]);

  // Fetch active missions from backend
  const fetchActiveMissions = useCallback(async () => {
    try {
      // Get missions that are not completed/cancelled/returned
      const missions = await getMissions({ status: 'pending,en_route,arrived' });
      if (missions.length > 0) {
        const mission = missions[0];
        // Find patient by bedId
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

  // Fetch history data
  useEffect(() => {
    const fetchHistory = async () => {
      try {
        // Fetch delivery history
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

        // Fetch biped session history
        const bipedRes = await getBipedSessionHistory({ limit: 50 });
        setBipedHistory(bipedRes.history.map(item => ({
          id: item._id,
          date: item.startTime,
          robotId: item.robotId,
          robotName: item.robotName,
          userId: item.userId,
          userName: item.userName,
          patientId: item.patientId,
          patientName: item.patientName,
          totalSteps: item.totalSteps,
          duration: item.duration,
          status: item.status as 'completed' | 'active' | 'interrupted'
        })));

        // Also fetch active missions
        await fetchActiveMissions();
      } catch (error) {
        console.error('Failed to fetch history:', error);
      }
    };

    fetchHistory();
    // Refresh every 5 seconds for better real-time updates
    const interval = setInterval(fetchHistory, 5000);
    return () => clearInterval(interval);
  }, [fetchActiveMissions]);

  const handleBedClick = (bedId: string, patient?: Patient) => {
    if (selectedBedId === bedId) {
      // Deselect if clicking the same bed
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
      // Set active mission with the returned missionId immediately
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
      {/* Left Side - Bed Map */}
      <div className="bg-white p-6 rounded-lg shadow-sm border">
        <h2 className="text-xl font-semibold mb-4 text-gray-900">Hospital Floor Map</h2>
        <BedMap 
          patients={patients} 
          robots={robots} 
          onBedClick={handleBedClick}
          selectedBedId={selectedBedId}
        />
      </div>

      {/* Right Side - Robot Management */}
      <div className="bg-white p-6 rounded-lg shadow-sm border">
        <RobotManagement 
          robots={robots} 
          selectedPatient={selectedPatient}
          onSendRobot={handleSendRobot}
          onCancelTask={handleCancelTask}
          activeMission={activeMission}
          deliveryHistory={deliveryHistory}
          bipedHistory={bipedHistory}
        />
      </div>
    </div>
  );
}