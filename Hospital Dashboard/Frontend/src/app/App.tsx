import { useState } from 'react';
import { Users, Bot, Hospital, RefreshCw, Bell, AlertCircle } from 'lucide-react';
import { Button } from '@/app/components/ui/button';
import { PatientDashboard } from '@/app/components/PatientDashboard';
import { RobotCenter } from '@/app/components/RobotCenter';
import { PatientDetails } from '@/app/components/PatientDetails';
import { ConnectionStatus } from '@/app/components/ConnectionStatus';
import { Patient } from '@/app/types/patient';
import { usePatients } from '@/app/hooks/usePatients';
import { useRobots } from '@/app/hooks/useRobots';
import { useAlerts } from '@/app/hooks/useAlerts';
import { useMissions } from '@/app/hooks/useMissions';

type Module = 'patients' | 'robot';

export default function App() {
  const [currentModule, setCurrentModule] = useState<Module>('patients');
  const [selectedPatientForDetails, setSelectedPatientForDetails] = useState<Patient | null>(null);
  
  // Use API hooks for data fetching
  const { 
    patients, 
    loading: patientsLoading, 
    error: patientsError,
    addPatient,
    updatePatient,
    deletePatient,
    refresh: refreshPatients 
  } = usePatients();
  
  const { 
    robots, 
    loading: robotsLoading, 
    error: robotsError,
    cancelTask,
    refresh: refreshRobots 
  } = useRobots(5000); // Poll every 5 seconds
  
  const { 
    alerts, 
    alertCounts,
    resolveAlert,
    refresh: refreshAlerts 
  } = useAlerts(10000); // Poll every 10 seconds

  const {
    createDeliveryMission,
    cancelDeliveryMission,
  } = useMissions();

  const handleAddPatient = async (patient: Patient) => {
    try {
      await addPatient(patient);
    } catch (err) {
      console.error('Failed to add patient:', err);
    }
  };

  const handleUpdatePatient = async (updatedPatient: Patient) => {
    try {
      await updatePatient(updatedPatient);
    } catch (err) {
      console.error('Failed to update patient:', err);
    }
  };

  const handleDeletePatient = async (id: string) => {
    try {
      await deletePatient(id);
    } catch (err) {
      console.error('Failed to delete patient:', err);
    }
  };

  const handleCancelRobotTask = async (missionId: string) => {
    try {
      await cancelDeliveryMission(missionId);
    } catch (err) {
      console.error('Failed to cancel mission:', err);
    }
  };

  const handleSendRobot = async (patientId: string): Promise<{ missionId: string; robotId: string } | null> => {
    // Find the patient to get the bed location
    const patient = patients.find(p => p.id === patientId);
    if (!patient) {
      console.error('Patient not found:', patientId);
      return null;
    }

    // Check if patient has a bed assigned
    if (!patient.roomBedId) {
      console.error('Patient has no bed assigned:', patient.fullName);
      alert('Bệnh nhân chưa được gán giường!');
      return null;
    }

    // Check if there's an available Carry robot
    const availableRobot = robots.find(r => r.type === 'Carry' && r.status === 'Idle');
    if (!availableRobot) {
      console.error('No idle Carry robot available');
      alert('Không có Carry Robot nào đang rảnh!');
      return null;
    }

    try {
      console.log('Sending robot to patient:', patient.fullName, 'at bed:', patient.roomBedId);
      
      // Call API to create delivery mission
      const result = await createDeliveryMission({
        mapId: 'floor1', // Default map ID - can be made dynamic later
        bedId: patient.roomBedId,
        patientName: patient.fullName,
      });

      console.log('Mission created:', result);
      alert(`Đã gửi robot ${result.carryRobotId} đến giường ${patient.roomBedId}!\nMission ID: ${result.missionId}`);
      
      // Refresh robots to update status
      refreshRobots();
      
      return { missionId: result.missionId, robotId: result.carryRobotId };
    } catch (err) {
      console.error('Failed to create delivery mission:', err);
      const errorMsg = err instanceof Error ? err.message : 'Unknown error';
      alert(`Lỗi khi gửi robot: ${errorMsg}`);
      return null;
    }
  };

  const handleBedClick = (bedId: string, patient?: Patient) => {
    // Only show patient details dialog when in Patients Manager view, not in Robot Center
    if (currentModule === 'patients' && patient) {
      setSelectedPatientForDetails(patient);
    }
  };

  const handleRefresh = () => {
    refreshPatients();
    refreshRobots();
    refreshAlerts();
  };

  const isLoading = patientsLoading || robotsLoading;
  const hasError = patientsError || robotsError;

  return (
    <div className="min-h-screen bg-background flex flex-col">
      {/* Sticky Header + Nav Container */}
      <div className="sticky top-0 z-50">
        {/* Header */}
        <header className="bg-primary border-b shadow-sm">
          <div className="w-full px-6 py-4">
            <div className="flex items-center justify-between">
              <div className="flex items-center gap-4">
                <Hospital className="w-10 h-10 text-white" />
                <div>
                  <h1 className="text-3xl font-bold text-black">Hospital Management System</h1>
                  <p className="text-base text-black opacity-80">Patient Care & Robot Navigation</p>
                </div>
              </div>
            
            {/* Header Actions */}
            <div className="flex items-center gap-4">
              {/* Connection Status */}
              <ConnectionStatus />
              
              {/* Alerts indicator */}
              {alertCounts.total > 0 && (
                <div className="flex items-center gap-2 bg-red-100 px-4 py-2 rounded-full">
                  <Bell className="w-5 h-5 text-red-600" />
                  <span className="text-base font-medium text-red-600">
                    {alertCounts.total} Alert{alertCounts.total > 1 ? 's' : ''}
                  </span>
                </div>
              )}
              
              {/* Refresh button */}
              <Button
                variant="outline"
                size="lg"
                onClick={handleRefresh}
                disabled={isLoading}
                className="bg-white text-base"
              >
                <RefreshCw className={`w-5 h-5 mr-2 ${isLoading ? 'animate-spin' : ''}`} />
                Refresh
              </Button>
            </div>
          </div>
        </div>
      </header>

        {/* Module Navigation */}
        <nav className="bg-card border-b">
          <div className="w-full px-6">
            <div className="flex gap-2">
              <Button
                variant={currentModule === 'patients' ? 'default' : 'ghost'}
                onClick={() => setCurrentModule('patients')}
                className="rounded-none border-b-2 border-transparent data-[active=true]:border-primary text-base py-3 px-6"
                data-active={currentModule === 'patients'}
              >
                <Users className="w-5 h-5 mr-2" />
                Patients Manager
              </Button>
              <Button
                variant={currentModule === 'robot' ? 'default' : 'ghost'}
                onClick={() => setCurrentModule('robot')}
                className="rounded-none border-b-2 border-transparent data-[active=true]:border-primary text-base py-3 px-6"
                data-active={currentModule === 'robot'}
              >
                <Bot className="w-5 h-5 mr-2" />
                Robot Center
              </Button>
            </div>
          </div>
        </nav>
      </div>

      {/* Main Content */}
      <main className="flex-1 w-full px-6 py-6">
        {currentModule === 'patients' ? (
          <PatientDashboard
            patients={patients}
            onAddPatient={handleAddPatient}
            onUpdatePatient={handleUpdatePatient}
            onDeletePatient={handleDeletePatient}
          />
        ) : (
          <RobotCenter
            patients={patients}
            robots={robots}
            onBedClick={handleBedClick}
            onCancelTask={handleCancelRobotTask}
            onSendRobot={handleSendRobot}
          />
        )}
      </main>

      {/* Patient Details from Bed Map */}
      <PatientDetails
        patient={selectedPatientForDetails}
        isOpen={!!selectedPatientForDetails}
        onClose={() => setSelectedPatientForDetails(null)}
        onUpdatePatient={handleUpdatePatient}
      />

      {/* Footer */}
      <footer className="bg-white border-t py-4">
        <div className="w-full px-6">
          <p className="text-center text-base text-gray-600">
            Hospital Patient Management System • Integrated with Robot Navigation • Secure RFID Access Control
          </p>
        </div>
      </footer>
    </div>
  );
}