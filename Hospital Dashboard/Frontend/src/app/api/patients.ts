import { API_ENDPOINTS } from './config';
import { get, post, put, del, upload, uploadPut } from './http';

// Backend Prescription interface
export interface BackendPrescription {
  _id?: string;
  medication: string;
  dosage: string;
  frequency: string;
  startDate?: string;
  endDate?: string;
  prescribedBy?: string;
  instructions?: string;
  status?: string;
}

// Backend Note interface
export interface BackendNote {
  _id?: string;
  text: string;
  createdBy?: string;
  createdAt?: string;
}

// Backend Patient interface (matching backend model)
export interface BackendPatient {
  _id: string;
  mrn: string;
  fullName: string;
  cardNumber?: string;
  admissionDate?: string;
  status: string;
  primaryDoctor?: string;
  department?: string;
  ward?: string;
  roomBed?: string;
  insurancePolicyId?: string;
  relativeName?: string;
  relativePhone?: string;
  photoUrl?: string;
  photoPath?: string;
  allergens?: string[];
  prescriptions?: BackendPrescription[];
  notes?: BackendNote[];
  createdAt?: string;
  updatedAt?: string;
}

// Create patient data interface
export interface CreatePatientData {
  mrn: string;
  fullName: string;
  cardNumber?: string;
  admissionDate?: string;
  status?: string;
  primaryDoctor?: string;
  department?: string;
  ward?: string;
  roomBed?: string;
  insurancePolicyId?: string;
  relativeName?: string;
  relativePhone?: string;
  allergens?: string[];
  notes?: string;
}

export interface BedInfo {
  bedId: string;
  patientId?: string;
  patientName?: string;
  status?: string;
}

export interface OccupancyInfo {
  total: number;
  occupied: number;
  available: number;
  beds: BedInfo[];
}

// Get all patients
export async function getPatients(params?: {
  status?: string;
  department?: string;
  doctor?: string;
  search?: string;
}): Promise<BackendPatient[]> {
  return get<BackendPatient[]>(API_ENDPOINTS.patients, { params });
}

// Get patient by ID
export async function getPatientById(id: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`);
}

// Get patient by MRN
export async function getPatientByMrn(mrn: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/mrn/${mrn}`);
}

// Get patient by RFID card number
export async function getPatientByCard(cardNumber: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/card/${cardNumber}`);
}

// Create new patient
export async function createPatient(data: CreatePatientData): Promise<BackendPatient> {
  return post<BackendPatient>(API_ENDPOINTS.patients, data);
}

// Create patient with photo
export async function createPatientWithPhoto(data: CreatePatientData, photo?: File): Promise<BackendPatient> {
  const formData = new FormData();
  
  // Add all data fields
  Object.entries(data).forEach(([key, value]) => {
    if (value !== undefined && value !== null) {
      if (Array.isArray(value)) {
        formData.append(key, JSON.stringify(value));
      } else {
        formData.append(key, String(value));
      }
    }
  });
  
  // Add photo if provided
  if (photo) {
    formData.append('photo', photo);
  }
  
  return upload<BackendPatient>(API_ENDPOINTS.patients, formData);
}

// Update patient
export async function updatePatient(id: string, data: Partial<CreatePatientData>): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, data);
}

// Update patient with photo
export async function updatePatientWithPhoto(id: string, data: Partial<CreatePatientData>, photo?: File): Promise<BackendPatient> {
  const formData = new FormData();
  
  // Add all data fields
  Object.entries(data).forEach(([key, value]) => {
    if (value !== undefined && value !== null) {
      if (Array.isArray(value)) {
        formData.append(key, JSON.stringify(value));
      } else {
        formData.append(key, String(value));
      }
    }
  });
  
  // Add photo if provided
  if (photo) {
    formData.append('photo', photo);
  }
  
  return uploadPut<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, formData);
}

// Delete patient
export async function deletePatient(id: string): Promise<{ ok: boolean; message?: string }> {
  return del<{ ok: boolean; message?: string }>(`${API_ENDPOINTS.patients}/${id}`);
}

// Assign patient to bed
export async function assignPatientToBed(id: string, bedId: string): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}/bed`, { roomBed: bedId });
}

// Get all beds
export async function getBeds(): Promise<BedInfo[]> {
  return get<BedInfo[]>(API_ENDPOINTS.patientsBeds);
}

// Get bed occupancy info
export async function getOccupancy(): Promise<OccupancyInfo> {
  return get<OccupancyInfo>(API_ENDPOINTS.patientsOccupancy);
}

// Discharge patient
export async function dischargePatient(id: string): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, { 
    status: 'Discharged',
    roomBed: null 
  });
}
