import { useState, useRef, useEffect } from 'react';
import { Users, Bot, Hospital, RefreshCw, Bell, X, CheckCheck, FlaskConical } from 'lucide-react';
import { Button } from '@/app/components/ui/button';
import { PatientDashboard } from '@/app/components/PatientDashboard';
import { RobotCenter } from '@/app/components/RobotCenter';
import { RobotTestLab } from '@/app/components/RobotTestLab';
import { PatientDetails } from '@/app/components/PatientDetails';
import { ConnectionStatus } from '@/app/components/ConnectionStatus';
import { RFIDProvider } from '@/app/contexts/RFIDContext';
import { Patient } from '@/app/types/patient';
import { usePatients } from '@/app/hooks/usePatients';
import { useRobots } from '@/app/hooks/useRobots';
import { useAlerts } from '@/app/hooks/useAlerts';
import { useMissions } from '@/app/hooks/useMissions';

type Module = 'patients' | 'robot' | 'lab';

export default function App() {
  const [currentModule, setCurrentModule] = useState<Module>('patients');
  const [selectedPatientForDetails, setSelectedPatientForDetails] = useState<Patient | null>(null);

  const {
    patients,
    loading: patientsLoading,
    addPatient,
    updatePatient,
    deletePatient,
    refresh: refreshPatients
  } = usePatients();

  const {
    robots,
    loading: robotsLoading,
    refresh: refreshRobots
  } = useRobots(5000);

  const {
    alerts,
    alertCounts,
    resolveAlert,
    refresh: refreshAlerts
  } = useAlerts(10000);

  const [alertPanelOpen, setAlertPanelOpen] = useState(false);
  const alertPanelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      if (alertPanelRef.current && !alertPanelRef.current.contains(e.target as Node)) {
        setAlertPanelOpen(false);
      }
    };
    if (alertPanelOpen) document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, [alertPanelOpen]);

  const {
    createDeliveryMission,
    cancelDeliveryMission,
  } = useMissions();

  const handleAddPatient = async (patient: Patient, photoFile?: File) => {
    try {
      await addPatient(patient, photoFile);
    } catch (err) {
      console.error('Failed to add patient:', err);
    }
  };

  const handleUpdatePatient = async (updatedPatient: Patient, photoFile?: File) => {
    try {
      await updatePatient(updatedPatient, photoFile);
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

    const patient = patients.find(p => p.id === patientId);
    if (!patient) {
      console.error('Patient not found:', patientId);
      return null;
    }

    if (!patient.roomBedId) {
      console.error('Patient has no bed assigned:', patient.fullName);
      alert('Patient has no bed assigned!');
      return null;
    }

    const availableRobot = robots.find(r => r.type === 'Carry' && r.status === 'Idle');
    if (!availableRobot) {
      console.error('No idle Carry robot available');
      alert('No Carry Robot available!');
      return null;
    }

    try {
      console.log('Sending robot to patient:', patient.fullName, 'at bed:', patient.roomBedId);

      const result = await createDeliveryMission({
        mapId: 'floor1',
        bedId: patient.roomBedId,
        patientName: patient.fullName,
      });

      console.log('Mission created:', result);
      alert(`Đã gửi robot ${result.carryRobotId} đến giường ${patient.roomBedId}!\nMission ID: ${result.missionId}`);

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

  return (
    <RFIDProvider>
    <div className="min-h-screen bg-background flex flex-col">
      {}
      <div className="sticky top-0 z-50">
        {}
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

            {}
            <div className="flex items-center gap-4">
              {}
              <ConnectionStatus />

              {/* Alert bell */}
              <div className="relative" ref={alertPanelRef}>
                <button
                  onClick={() => setAlertPanelOpen(p => !p)}
                  className={`flex items-center gap-2 px-4 py-2 rounded-full transition-colors ${
                    alertCounts.total > 0
                      ? 'bg-red-100 hover:bg-red-200 text-red-600'
                      : 'bg-gray-100 hover:bg-gray-200 text-gray-500'
                  }`}
                >
                  <Bell className="w-5 h-5" />
                  <span className="text-base font-medium">
                    {alertCounts.total > 0
                      ? `${alertCounts.total} Alert${alertCounts.total > 1 ? 's' : ''}`
                      : 'No Alerts'}
                  </span>
                </button>

                {alertPanelOpen && (
                  <div className="absolute right-0 top-12 w-96 bg-white border border-gray-200 rounded-xl shadow-xl z-50 overflow-hidden">
                    <div className="flex items-center justify-between px-4 py-3 border-b bg-gray-50">
                      <span className="font-semibold text-gray-700">Active Alerts</span>
                      {alertCounts.total > 0 && (
                        <button
                          onClick={async () => {
                            await Promise.all(alerts.map(a => resolveAlert(a._id)));
                          }}
                          className="flex items-center gap-1 text-sm text-blue-600 hover:text-blue-800"
                          title="Dismiss all"
                        >
                          <CheckCheck className="w-4 h-4" />
                          Dismiss all
                        </button>
                      )}
                    </div>

                    {alerts.length === 0 ? (
                      <p className="px-4 py-6 text-center text-sm text-gray-400">No active alerts</p>
                    ) : (
                      <ul className="max-h-80 overflow-y-auto divide-y">
                        {alerts.map(a => (
                          <li key={a._id} className="flex items-start gap-3 px-4 py-3 hover:bg-gray-50">
                            <span className={`mt-0.5 w-2 h-2 rounded-full flex-shrink-0 ${
                              a.level === 'high' ? 'bg-red-500' :
                              a.level === 'medium' ? 'bg-yellow-400' : 'bg-blue-400'
                            }`} />
                            <div className="flex-1 min-w-0">
                              <p className="text-sm text-gray-800 break-words">{a.message}</p>
                              <p className="text-xs text-gray-400 mt-0.5">
                                {a.type}{a.robotId ? ` · ${a.robotId}` : ''}
                                {' · '}{new Date(a.createdAt).toLocaleString()}
                              </p>
                            </div>
                            <button
                              onClick={() => resolveAlert(a._id)}
                              className="text-gray-400 hover:text-red-500 flex-shrink-0"
                              title="Dismiss"
                            >
                              <X className="w-4 h-4" />
                            </button>
                          </li>
                        ))}
                      </ul>
                    )}
                  </div>
                )}
              </div>

              {}
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

        {}
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
              <Button
                variant={currentModule === 'lab' ? 'default' : 'ghost'}
                onClick={() => setCurrentModule('lab')}
                className="rounded-none border-b-2 border-transparent data-[active=true]:border-primary text-base py-3 px-6"
                data-active={currentModule === 'lab'}
              >
                <FlaskConical className="w-5 h-5 mr-2" />
                Robot test lab
              </Button>
            </div>
          </div>
        </nav>
      </div>

      {}
      <main className="flex-1 w-full px-6 py-6">
        {currentModule === 'patients' ? (
          <PatientDashboard
            patients={patients}
            onAddPatient={handleAddPatient}
            onUpdatePatient={handleUpdatePatient}
            onDeletePatient={handleDeletePatient}
          />
        ) : currentModule === 'robot' ? (
          <RobotCenter
            patients={patients}
            robots={robots}
            onBedClick={handleBedClick}
            onCancelTask={handleCancelRobotTask}
            onSendRobot={handleSendRobot}
          />
        ) : (
          <RobotTestLab />
        )}
      </main>

      {}
      <PatientDetails
        patient={selectedPatientForDetails}
        isOpen={!!selectedPatientForDetails}
        onClose={() => setSelectedPatientForDetails(null)}
        onUpdatePatient={handleUpdatePatient}
      />

      {}
      <footer className="bg-white border-t py-4">
        <div className="w-full px-6">
          <p className="text-center text-base text-gray-600">
            Hospital Patient Management System • Integrated with Robot Navigation • Secure RFID Access Control
          </p>
        </div>
      </footer>
    </div>
    </RFIDProvider>
  );
}
