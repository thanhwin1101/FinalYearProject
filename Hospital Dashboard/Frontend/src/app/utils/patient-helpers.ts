import { Patient } from '@/app/types/patient';

export const generateMRN = (): string => {
  const year = new Date().getFullYear();
  const random = Math.floor(Math.random() * 1000).toString().padStart(3, '0');
  return `MRN-${year}-${random}`;
};

export const exportToCSV = (patients: Patient[]): void => {
  const headers = [
    'MRN',
    'Full Name',
    'Card Number',
    'Admission Date',
    'Status',
    'Primary Doctor',
    'Department',
    'Ward',
    'Room-Bed ID',
    'Insurance/Policy ID',
    'Relative Name',
    'Relative Phone'
  ];

  const rows = patients.map(patient => [
    patient.mrn,
    patient.fullName,
    patient.cardNumber,
    patient.admissionDate,
    patient.status,
    patient.primaryDoctor,
    patient.department,
    patient.ward,
    patient.roomBedId,
    patient.insurancePolicyId || '',
    patient.relativeName,
    patient.relativePhone
  ]);

  const csvContent = [
    headers.join(','),
    ...rows.map(row => row.map(cell => `"${cell}"`).join(','))
  ].join('\n');

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const link = document.createElement('a');
  const url = URL.createObjectURL(blob);
  
  link.setAttribute('href', url);
  link.setAttribute('download', `patients_export_${new Date().toISOString().split('T')[0]}.csv`);
  link.style.visibility = 'hidden';
  
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
};

export const sortPatientsByBedOrder = (patients: Patient[]): Patient[] => {
  return [...patients].sort((a, b) => {
    return a.roomBedId.localeCompare(b.roomBedId);
  });
};
