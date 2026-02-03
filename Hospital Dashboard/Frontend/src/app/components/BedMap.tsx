import { useState } from 'react';
import { Patient } from '@/app/types/patient';
import { Robot } from '@/app/types/robot';
import { User, Bot } from 'lucide-react';

interface BedMapProps {
  patients: Patient[];
  robots: Robot[];
  onBedClick?: (bedId: string, patient?: Patient) => void;
  selectedBedId?: string | null;
}

interface BedInfo {
  id: string;
  patient?: Patient;
  hasRobot: boolean;
}

export function BedMap({ patients, robots, onBedClick, selectedBedId }: BedMapProps) {
  const getBedInfo = (bedId: string): BedInfo => {
    const patient = patients.find(p => p.roomBedId === bedId);
    const hasRobot = robots.some(r => r.currentLocation === bedId || r.destination === bedId);
    return { id: bedId, patient, hasRobot };
  };

  const handleBedClick = (bedId: string, patient?: Patient) => {
    onBedClick?.(bedId, patient);
  };

  const renderRoom = (roomNumber: number, beds: string[]) => {
    const mBeds = beds.filter(b => b.includes('M'));
    const oBeds = beds.filter(b => b.includes('O'));

    // Room 1 & 3: O beds on left, M beds on right
    // Room 2 & 4: M beds on left, O beds on right
    const leftBeds = (roomNumber === 1 || roomNumber === 3) ? oBeds : mBeds;
    const rightBeds = (roomNumber === 1 || roomNumber === 3) ? mBeds : oBeds;

    return (
      <div className="border-2 border-gray-300 rounded-lg p-3 bg-white relative">
        <div className="grid grid-cols-2 gap-3">
          {/* Left column */}
          <div className="space-y-2">
            {leftBeds.map(bedId => {
              const bedInfo = getBedInfo(bedId);
              return (
                <BedCell
                  key={bedId}
                  bedInfo={bedInfo}
                  onClick={() => handleBedClick(bedId, bedInfo.patient)}
                  isSelected={selectedBedId === bedId}
                />
              );
            })}
          </div>

          {/* Right column */}
          <div className="space-y-2">
            {rightBeds.map(bedId => {
              const bedInfo = getBedInfo(bedId);
              return (
                <BedCell
                  key={bedId}
                  bedInfo={bedInfo}
                  onClick={() => handleBedClick(bedId, bedInfo.patient)}
                  isSelected={selectedBedId === bedId}
                />
              );
            })}
          </div>
        </div>
        <div className="text-center mt-2 text-xs font-medium text-gray-600">
          Room {roomNumber}
        </div>
      </div>
    );
  };

  return (
    <div className="space-y-4">
      {/* Floor Layout */}
      <div className="grid grid-cols-2 gap-4">
        {/* Room 1 */}
        {renderRoom(1, ['R1O1', 'R1O2', 'R1O3', 'R1M1', 'R1M2', 'R1M3'])}

        {/* Room 2 */}
        {renderRoom(2, ['R2O1', 'R2O2', 'R2O3', 'R2M1', 'R2M2', 'R2M3'])}
      </div>

      <div className="grid grid-cols-2 gap-4">
        {/* Room 3 */}
        {renderRoom(3, ['R3O1', 'R3O2', 'R3O3', 'R3M1', 'R3M2', 'R3M3'])}

        {/* Room 4 */}
        {renderRoom(4, ['R4O1', 'R4O2', 'R4O3', 'R4M1', 'R4M2', 'R4M3'])}
      </div>

      {/* Legend */}
      <div className="flex gap-4 justify-center text-xs text-gray-900">
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-green-100 border border-green-300 rounded"></div>
          <span className="font-medium">Occupied</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-gray-100 border border-gray-300 rounded"></div>
          <span className="font-medium">Available</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-blue-100 border border-blue-300 rounded"></div>
          <span className="font-medium">Robot Active</span>
        </div>
      </div>
    </div>
  );
}

function BedCell({ bedInfo, onClick, isSelected }: { 
  bedInfo: BedInfo; 
  onClick: () => void;
  isSelected: boolean;
}) {
  const { id, patient, hasRobot } = bedInfo;

  const bgColor = patient
    ? hasRobot
      ? 'bg-blue-100 border-blue-400'
      : 'bg-green-100 border-green-400'
    : 'bg-gray-50 border-gray-300';

  return (
    <button
      onClick={onClick}
      className={`w-full h-20 p-2 border-2 rounded transition-all hover:shadow-md ${bgColor} ${
        isSelected ? 'ring-2 ring-primary ring-offset-2' : ''
      } ${patient ? 'cursor-pointer' : 'cursor-default'}`}
    >
      <div className="flex flex-col h-full justify-between">
        <div className="flex items-center justify-between">
          <span className="font-semibold text-xs text-gray-900">{id}</span>
          <div className="flex gap-1">
            {patient && <User className="w-3 h-3 text-gray-700" />}
            {hasRobot && <Bot className="w-3 h-3 text-blue-700" />}
          </div>
        </div>
        {patient && (
          <div className="text-left">
            <div className="font-medium text-xs leading-tight text-gray-900">{patient.fullName}</div>
          </div>
        )}
      </div>
    </button>
  );
}