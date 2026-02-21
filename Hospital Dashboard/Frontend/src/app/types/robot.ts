export type RobotType = 'Biped' | 'Carry';
export type RobotStatus = 'Idle' | 'Moving' | 'At Destination' | 'Task in Progress' | 'Error';

export interface Robot {
  id: string;
  type: RobotType;
  name: string;
  currentLocation: string;
  destination: string;
  currentNode?: string;
  status: RobotStatus;
  assignedPatientId?: string;
  taskDescription?: string;
  batteryLevel: number;
  lastUpdated: string;
}

export interface RobotTask {
  id: string;
  robotId: string;
  patientId?: string;
  fromLocation: string;
  toLocation: string;
  taskType: string;
  status: 'Pending' | 'In Progress' | 'Completed' | 'Cancelled';
  createdAt: string;
}
