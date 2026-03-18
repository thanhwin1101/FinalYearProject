import { API_ENDPOINTS } from './config';
import { get, post, put, del, upload, uploadPut } from './http';

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

export interface BackendNote {
  _id?: string;
  text: string;
  createdBy?: string;
  createdAt?: string;
}

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

export async function getPatients(params?: {
  status?: string;
  department?: string;
  doctor?: string;
  search?: string;
}): Promise<BackendPatient[]> {
  return get<BackendPatient[]>(API_ENDPOINTS.patients, { params });
}

export async function getPatientById(id: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`);
}

export async function getPatientByMrn(mrn: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/mrn/${mrn}`);
}

export async function getPatientByCard(cardNumber: string): Promise<BackendPatient> {
  return get<BackendPatient>(`${API_ENDPOINTS.patients}/card/${cardNumber}`);
}

export async function createPatient(data: CreatePatientData): Promise<BackendPatient> {
  return post<BackendPatient>(API_ENDPOINTS.patients, data);
}

export async function createPatientWithPhoto(data: CreatePatientData, photo?: File): Promise<BackendPatient> {
  const formData = new FormData();

  Object.entries(data).forEach(([key, value]) => {
    if (value !== undefined && value !== null) {
      if (Array.isArray(value)) {
        formData.append(key, JSON.stringify(value));
      } else {
        formData.append(key, String(value));
      }
    }
  });

  if (photo) {
    formData.append('photo', photo);
  }

  return upload<BackendPatient>(API_ENDPOINTS.patients, formData);
}

export async function updatePatient(id: string, data: Partial<CreatePatientData>): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, data);
}

export async function updatePatientWithPhoto(id: string, data: Partial<CreatePatientData>, photo?: File): Promise<BackendPatient> {
  const formData = new FormData();

  Object.entries(data).forEach(([key, value]) => {
    if (value !== undefined && value !== null) {
      if (Array.isArray(value)) {
        formData.append(key, JSON.stringify(value));
      } else {
        formData.append(key, String(value));
      }
    }
  });

  if (photo) {
    formData.append('photo', photo);
  }

  return uploadPut<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, formData);
}

export async function deletePatient(id: string): Promise<{ ok: boolean; message?: string }> {
  return del<{ ok: boolean; message?: string }>(`${API_ENDPOINTS.patients}/${id}`);
}

export async function assignPatientToBed(id: string, bedId: string): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}/bed`, { roomBed: bedId });
}

export async function getBeds(): Promise<BedInfo[]> {
  return get<BedInfo[]>(API_ENDPOINTS.patientsBeds);
}

export async function getOccupancy(): Promise<OccupancyInfo> {
  return get<OccupancyInfo>(API_ENDPOINTS.patientsOccupancy);
}

export async function dischargePatient(id: string): Promise<BackendPatient> {
  return put<BackendPatient>(`${API_ENDPOINTS.patients}/${id}`, {
    status: 'Discharged',
    roomBed: null
  });
}
