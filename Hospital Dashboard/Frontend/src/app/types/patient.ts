export interface Patient {
  id: string;
  mrn: string; // Medical Record Number
  fullName: string;
  cardNumber: string; // RFID/UID
  admissionDate: string;
  status: PatientStatus;
  primaryDoctor: string;
  department: string;
  ward: string;
  roomBedId: string;
  insurancePolicyId?: string;
  relativeName: string;
  relativePhone: string;
  photo?: string; // Base64 or URL
  medicationLog: MedicationEntry[];
  allergens: string[];
  notes: string;
  createdAt: string;
  updatedAt: string;
}

export interface MedicationEntry {
  id: string;
  name: string;
  dosage: string;
  frequency: string;
  prescribedBy: string;
  startDate: string;
  endDate?: string;
  notes?: string;
}

export type PatientStatus = 'Stable' | 'Critical' | 'Recovering' | 'Under Observation' | 'Discharged';

export const PATIENT_STATUSES: PatientStatus[] = [
  'Stable',
  'Critical',
  'Recovering',
  'Under Observation',
  'Discharged'
];

export const BED_MAP = [
  'R1M1', 'R1M2', 'R1M3',
  'R1O1', 'R1O2', 'R1O3',
  'R2M1', 'R2M2', 'R2M3',
  'R2O1', 'R2O2', 'R2O3',
  'R3M1', 'R3M2', 'R3M3',
  'R3O1', 'R3O2', 'R3O3',
  'R4M1', 'R4M2', 'R4M3',
  'R4O1', 'R4O2', 'R4O3'
];

export interface SearchFilters {
  quickSearch: string;
  status: string;
  primaryDoctor: string;
  department: string;
  dateFrom: string;
  dateTo: string;
}