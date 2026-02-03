import { useState, useEffect, useCallback } from 'react';
import { 
  getPatients, 
  createPatientWithPhoto, 
  updatePatientWithPhoto, 
  deletePatient as apiDeletePatient,
  BackendPatient,
  CreatePatientData
} from '@/app/api/patients';
import { Patient, MedicationEntry } from '@/app/types/patient';

// Convert backend patient to frontend patient format
function toFrontendPatient(bp: BackendPatient): Patient {
  // Convert prescriptions to medicationLog format
  const medicationLog: MedicationEntry[] = (bp.prescriptions || []).map((p, idx) => ({
    id: p._id || `med-${idx}`,
    name: p.medication || '',
    dosage: p.dosage || '',
    frequency: p.frequency || '',
    prescribedBy: p.prescribedBy || '',
    startDate: p.startDate || '',
    endDate: p.endDate,
    notes: p.instructions
  }));

  // Convert notes array to string for display
  const notesText = (bp.notes || []).map(n => {
    const date = n.createdAt ? new Date(n.createdAt).toLocaleDateString() : '';
    const by = n.createdBy ? ` - ${n.createdBy}` : '';
    return `[${date}${by}] ${n.text}`;
  }).join('\n\n');

  return {
    id: bp._id,
    mrn: bp.mrn || '',
    fullName: bp.fullName || '',
    cardNumber: bp.cardNumber || '',
    admissionDate: bp.admissionDate?.slice(0, 10) || '',
    status: (bp.status as Patient['status']) || 'Stable',
    primaryDoctor: bp.primaryDoctor || '',
    department: bp.department || '',
    ward: bp.ward || '',
    roomBedId: bp.roomBed || '',
    insurancePolicyId: bp.insurancePolicyId || '',
    relativeName: bp.relativeName || '',
    relativePhone: bp.relativePhone || '',
    photo: bp.photoUrl || undefined,
    medicationLog,
    allergens: bp.allergens || [],
    notes: notesText,
    createdAt: bp.createdAt || new Date().toISOString(),
    updatedAt: bp.updatedAt || new Date().toISOString(),
  };
}

// Convert frontend patient to backend format
function toBackendPatientData(patient: Partial<Patient>): CreatePatientData {
  return {
    mrn: patient.mrn || '',
    fullName: patient.fullName || '',
    cardNumber: patient.cardNumber,
    admissionDate: patient.admissionDate,
    status: patient.status,
    primaryDoctor: patient.primaryDoctor,
    department: patient.department,
    ward: patient.ward,
    roomBed: patient.roomBedId,
    insurancePolicyId: patient.insurancePolicyId,
    relativeName: patient.relativeName,
    relativePhone: patient.relativePhone,
    allergens: patient.allergens,
    notes: patient.notes,
  };
}

export function usePatients() {
  const [patients, setPatients] = useState<Patient[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  // Fetch all patients
  const fetchPatients = useCallback(async () => {
    try {
      setLoading(true);
      setError(null);
      const data = await getPatients();
      setPatients(data.map(toFrontendPatient));
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to fetch patients';
      setError(message);
      console.error('Failed to fetch patients:', err);
    } finally {
      setLoading(false);
    }
  }, []);

  // Initial fetch
  useEffect(() => {
    fetchPatients();
  }, [fetchPatients]);

  // Add new patient
  const addPatient = useCallback(async (patient: Patient, photoFile?: File) => {
    try {
      setError(null);
      const backendData = toBackendPatientData(patient);
      const created = await createPatientWithPhoto(backendData, photoFile);
      const frontendPatient = toFrontendPatient(created);
      setPatients(prev => [...prev, frontendPatient]);
      return frontendPatient;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to create patient';
      setError(message);
      console.error('Failed to create patient:', err);
      throw err;
    }
  }, []);

  // Update existing patient
  const updatePatient = useCallback(async (patient: Patient, photoFile?: File) => {
    try {
      setError(null);
      const backendData = toBackendPatientData(patient);
      const updated = await updatePatientWithPhoto(patient.id, backendData, photoFile);
      const frontendPatient = toFrontendPatient(updated);
      setPatients(prev => prev.map(p => p.id === patient.id ? frontendPatient : p));
      return frontendPatient;
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to update patient';
      setError(message);
      console.error('Failed to update patient:', err);
      throw err;
    }
  }, []);

  // Delete patient
  const deletePatient = useCallback(async (id: string) => {
    try {
      setError(null);
      await apiDeletePatient(id);
      setPatients(prev => prev.filter(p => p.id !== id));
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to delete patient';
      setError(message);
      console.error('Failed to delete patient:', err);
      throw err;
    }
  }, []);

  // Refresh data
  const refresh = useCallback(() => {
    fetchPatients();
  }, [fetchPatients]);

  return {
    patients,
    loading,
    error,
    addPatient,
    updatePatient,
    deletePatient,
    refresh,
  };
}
