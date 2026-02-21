import { useState, useEffect, useCallback } from 'react';
import { useForm } from 'react-hook-form';
import { X, Save, RotateCcw, Sparkles, CalendarIcon, Radio } from 'lucide-react';
import { useRFIDContext } from '@/app/contexts/RFIDContext';
import DatePicker from 'react-datepicker';
import 'react-datepicker/dist/react-datepicker.css';
import { Button } from '@/app/components/ui/button';
import { Input } from '@/app/components/ui/input';
import { Label } from '@/app/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/app/components/ui/select';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/app/components/ui/dialog';
import { CameraCapture } from '@/app/components/CameraCapture';
import { Patient, PATIENT_STATUSES, BED_MAP } from '@/app/types/patient';
import { generateMRN } from '@/app/utils/patient-helpers';

interface PatientFormProps {
  isOpen: boolean;
  onClose: () => void;
  onSave: (patient: Patient, photoFile?: File) => void;
  editingPatient?: Patient | null;
  existingPatients?: Patient[]; // For duplicate card check
}

interface FormData {
  fullName: string;
  mrn: string;
  cardNumber: string;
  admissionDate: string;
  status: string;
  primaryDoctor: string;
  department: string;
  ward: string;
  roomBedId: string;
  insurancePolicyId: string;
  relativeName: string;
  relativePhone: string;
}

// Helper function to convert base64 to File
function base64ToFile(base64: string, filename: string): File {
  const arr = base64.split(',');
  const mimeMatch = arr[0].match(/:(.*?);/);
  const mime = mimeMatch ? mimeMatch[1] : 'image/jpeg';
  const bstr = atob(arr[1]);
  let n = bstr.length;
  const u8arr = new Uint8Array(n);
  while (n--) {
    u8arr[n] = bstr.charCodeAt(n);
  }
  return new File([u8arr], filename, { type: mime });
}

export function PatientForm({ isOpen, onClose, onSave, editingPatient, existingPatients = [] }: PatientFormProps) {
  const [photo, setPhoto] = useState<string>(''); // base64 for preview
  const [photoFile, setPhotoFile] = useState<File | null>(null); // File for upload
  const [selectedBedId, setSelectedBedId] = useState<string>('');
  const [duplicateCardError, setDuplicateCardError] = useState<string>('');
  const { register, handleSubmit, reset, setValue, watch, formState: { errors } } = useForm<FormData>();

  const mrnValue = watch('mrn');
  const cardNumberValue = watch('cardNumber');

  // RFID Reader integration via global context
  const { status: rfidStatus, isSupported: rfidSupported, onCardRead } = useRFIDContext();

  // Check for duplicate card number
  const checkDuplicateCard = useCallback((uid: string) => {
    const duplicate = existingPatients.find(
      p => p.cardNumber?.toUpperCase() === uid.toUpperCase() && p.id !== editingPatient?.id
    );
    if (duplicate) {
      setDuplicateCardError(`⚠️ This card is already assigned to patient: ${duplicate.fullName} (${duplicate.mrn})`);
      return true;
    }
    setDuplicateCardError('');
    return false;
  }, [existingPatients, editingPatient]);

  // Subscribe to card read events when form is open
  useEffect(() => {
    if (isOpen) {
      console.log('[PatientForm] Subscribing to RFID events');
      const unsubscribe = onCardRead((uid: string) => {
        console.log('[PatientForm] Received UID:', uid);
        setValue('cardNumber', uid, { shouldValidate: true });
        checkDuplicateCard(uid);
      });
      return () => {
        console.log('[PatientForm] Unsubscribing from RFID events');
        unsubscribe();
      };
    }
  }, [isOpen, onCardRead, setValue, checkDuplicateCard]);

  // Load form data when editing or opening
  useEffect(() => {
    if (isOpen) {
      if (editingPatient) {
        // Edit mode: load existing patient data
        reset({
          fullName: editingPatient.fullName,
          mrn: editingPatient.mrn,
          cardNumber: editingPatient.cardNumber,
          admissionDate: editingPatient.admissionDate,
          status: editingPatient.status,
          primaryDoctor: editingPatient.primaryDoctor,
          department: editingPatient.department,
          ward: editingPatient.ward,
          roomBedId: editingPatient.roomBedId,
          insurancePolicyId: editingPatient.insurancePolicyId || '',
          relativeName: editingPatient.relativeName,
          relativePhone: editingPatient.relativePhone
        });
        setPhoto(editingPatient.photo || '');
        setSelectedBedId(editingPatient.roomBedId || '');
      } else {
        // New patient mode: clear form
        reset({
          fullName: '',
          mrn: '',
          cardNumber: '',
          admissionDate: new Date().toISOString().split('T')[0],
          status: 'Stable',
          primaryDoctor: '',
          department: '',
          ward: '',
          roomBedId: '',
          insurancePolicyId: '',
          relativeName: '',
          relativePhone: ''
        });
        setPhoto('');
        setSelectedBedId('');
      }
    }
  }, [editingPatient, reset, isOpen]);

  const handleAutoGenerateMRN = () => {
    setValue('mrn', generateMRN());
  };

  const onSubmit = (data: FormData) => {
    // Check for duplicate before saving
    if (duplicateCardError) {
      return; // Don't save if duplicate card
    }
    
    const patient: Patient = {
      id: editingPatient?.id || crypto.randomUUID(),
      ...data,
      roomBedId: selectedBedId,
      status: data.status as Patient['status'],
      photo,
      medicationLog: editingPatient?.medicationLog || [],
      allergens: editingPatient?.allergens || [],
      notes: editingPatient?.notes || '',
      createdAt: editingPatient?.createdAt || new Date().toISOString(),
      updatedAt: new Date().toISOString()
    };
    onSave(patient, photoFile || undefined);
    handleReset();
    onClose();
  };

  const handleReset = () => {
    reset();
    setPhoto('');
    setPhotoFile(null);
    setSelectedBedId('');
    setDuplicateCardError('');
  };

  const handleBedIdChange = (value: string) => {
    setSelectedBedId(value);
    setValue('roomBedId', value);
  };

  return (
    <Dialog open={isOpen} onOpenChange={onClose}>
      <DialogContent className="max-w-6xl w-[95vw] max-h-[90vh] overflow-y-auto patient-form-dialog">
        <DialogHeader>
          <DialogTitle className="text-3xl">
            {editingPatient ? 'Edit Patient Record' : 'New Patient Registration'}
          </DialogTitle>
        </DialogHeader>

        <form onSubmit={handleSubmit(onSubmit)} className="space-y-8">
          {/* Biometric Integration */}
          <div className="space-y-3">
            <Label className="text-base font-semibold">Patient Photo (Biometric)</Label>
            <CameraCapture 
              onCapture={(base64) => {
                setPhoto(base64);
                // Convert base64 to File for upload
                if (base64) {
                  const file = base64ToFile(base64, `patient-photo-${Date.now()}.jpg`);
                  setPhotoFile(file);
                  console.log('[PatientForm] Photo captured, file size:', file.size);
                } else {
                  setPhotoFile(null);
                }
              }} 
              currentPhoto={photo} />
          </div>

          {/* Patient Identification */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div className="space-y-3">
              <Label htmlFor="fullName" className="text-base font-medium">Full Name *</Label>
              <Input
                id="fullName"
                {...register('fullName', { required: 'Full name is required' })}
                placeholder="Enter patient full name"
                className="h-12 text-base"
              />
              {errors.fullName && (
                <p className="text-base text-red-600">{errors.fullName.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="mrn" className="text-base font-medium">Patient ID / MRN *</Label>
              <div className="flex gap-3">
                <Input
                  id="mrn"
                  {...register('mrn', { required: 'MRN is required' })}
                  placeholder="MRN-YYYY-XXX"
                  className="flex-1 h-12 text-base"
                />
                <Button
                  type="button"
                  variant="outline"
                  onClick={handleAutoGenerateMRN}
                  className="shrink-0 h-12 text-base px-4"
                >
                  <Sparkles className="w-5 h-5 mr-2" />
                  Auto
                </Button>
              </div>
              {errors.mrn && (
                <p className="text-base text-red-600">{errors.mrn.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="cardNumber" className="text-base font-medium">Card Number (RFID/UID) *</Label>
              <div className="flex gap-3 items-center">
                <Input
                  id="cardNumber"
                  {...register('cardNumber', { required: 'Card number is required' })}
                  readOnly
                  placeholder={rfidStatus === 'connected' ? '🔄 Place card on reader to scan...' : '⚠️ Waiting for RFID reader connection...'}
                  className={`flex-1 h-12 text-base font-mono bg-gray-50 cursor-not-allowed ${rfidStatus === 'connected' ? 'border-green-300 focus:border-green-500' : 'border-orange-300'} ${duplicateCardError ? 'border-red-500' : ''}`}
                />
                {rfidStatus === 'connected' && (
                  <div className="flex items-center gap-2 text-green-600">
                    <Radio className="w-5 h-5 animate-pulse" />
                    <span className="text-sm font-medium">Ready</span>
                  </div>
                )}
              </div>
              {duplicateCardError && (
                <p className="text-sm text-red-600 font-medium bg-red-50 p-2 rounded border border-red-200">{duplicateCardError}</p>
              )}
              {rfidStatus === 'connected' && !duplicateCardError && (
                <p className="text-sm text-green-600">✓ RFID reader connected. Place card on reader to scan automatically.</p>
              )}
              {rfidStatus === 'disconnected' && rfidSupported && (
                <p className="text-sm text-orange-500">⚠️ RFID reader not connected. Please reload page and click "Connect" in popup.</p>
              )}
              {!rfidSupported && (
                <p className="text-xs text-red-500">❌ Browser does not support Web Serial API. Please use Chrome or Edge.</p>
              )}
              {errors.cardNumber && (
                <p className="text-base text-red-600">{errors.cardNumber.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="admissionDate" className="text-base font-medium">Admission Date *</Label>
              <div className="relative">
                <DatePicker
                  selected={watch('admissionDate') ? new Date(watch('admissionDate')) : null}
                  onChange={(date: Date | null) => setValue('admissionDate', date ? date.toISOString().split('T')[0] : '')}
                  dateFormat="dd/MM/yyyy"
                  placeholderText="Select date"
                  className="h-12 text-base w-full px-3 py-2 border border-input rounded-md bg-background cursor-pointer"
                  showPopperArrow={false}
                  popperPlacement="bottom-start"
                />
                <CalendarIcon className="absolute right-3 top-1/2 -translate-y-1/2 h-5 w-5 text-gray-500 pointer-events-none" />
              </div>
              <input type="hidden" {...register('admissionDate', { required: 'Admission date is required' })} />
              {errors.admissionDate && (
                <p className="text-base text-red-600">{errors.admissionDate.message}</p>
              )}
            </div>
          </div>

          {/* Medical & Admission Details */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div className="space-y-2">
              <Label htmlFor="status" className="text-base font-medium">Patient Status *</Label>
              <Select value={watch('status')} onValueChange={(value) => setValue('status', value)}>
                <SelectTrigger className="h-12 text-base">
                  <SelectValue placeholder="Select Status" />
                </SelectTrigger>
                <SelectContent>
                  {PATIENT_STATUSES.map(status => (
                    <SelectItem key={status} value={status}>{status}</SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <input type="hidden" {...register('status', { required: 'Status is required' })} />
              {errors.status && (
                <p className="text-base text-red-600">{errors.status.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="primaryDoctor" className="text-base font-medium">Primary Doctor *</Label>
              <Input
                id="primaryDoctor"
                {...register('primaryDoctor', { required: 'Primary doctor is required' })}
                placeholder="Dr. Name"
                className="h-12 text-base"
              />
              {errors.primaryDoctor && (
                <p className="text-base text-red-600">{errors.primaryDoctor.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="insurancePolicyId" className="text-base font-medium">Insurance / Policy ID</Label>
              <Input
                id="insurancePolicyId"
                {...register('insurancePolicyId')}
                placeholder="Optional"
                className="h-12 text-base"
              />
            </div>
          </div>

          {/* Location Mapping */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div className="space-y-3">
              <Label htmlFor="department" className="text-base font-medium">Department *</Label>
              <Input
                id="department"
                {...register('department', { required: 'Department is required' })}
                placeholder="e.g., Cardiology, ICU"
                className="h-12 text-base"
              />
              {errors.department && (
                <p className="text-base text-red-600">{errors.department.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="ward" className="text-base font-medium">Ward *</Label>
              <Input
                id="ward"
                {...register('ward', { required: 'Ward is required' })}
                placeholder="e.g., East Wing, West Wing"
                className="h-12 text-base"
              />
              {errors.ward && (
                <p className="text-base text-red-600">{errors.ward.message}</p>
              )}
            </div>

            <div className="space-y-2">
              <Label htmlFor="roomBedId" className="text-base font-medium">Room-Bed ID *</Label>
              <Select value={selectedBedId} onValueChange={handleBedIdChange}>
                <SelectTrigger className="h-12 text-base">
                  <SelectValue placeholder="Select Bed ID" />
                </SelectTrigger>
                <SelectContent>
                  {BED_MAP.map(bed => (
                    <SelectItem key={bed} value={bed}>{bed}</SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <input type="hidden" {...register('roomBedId', { required: 'Room-Bed ID is required' })} value={selectedBedId} />
              <p className="text-xs text-gray-400 italic">
                * Must match robot's navigation map
              </p>
              {errors.roomBedId && !selectedBedId && (
                <p className="text-base text-red-600">{errors.roomBedId.message}</p>
              )}
            </div>
          </div>

          {/* Emergency Contact */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div className="space-y-3">
              <Label htmlFor="relativeName" className="text-base font-medium">Relative/Guardian Name *</Label>
              <Input
                id="relativeName"
                {...register('relativeName', { required: 'Relative name is required' })}
                placeholder="Enter guardian name"
                className="h-12 text-base"
              />
              {errors.relativeName && (
                <p className="text-base text-red-600">{errors.relativeName.message}</p>
              )}
            </div>

            <div className="space-y-3">
              <Label htmlFor="relativePhone" className="text-base font-medium">Relative/Guardian Phone *</Label>
              <Input
                id="relativePhone"
                type="tel"
                {...register('relativePhone', { required: 'Relative phone is required' })}
                placeholder="Enter contact number"
                className="h-12 text-base"
              />
              {errors.relativePhone && (
                <p className="text-base text-red-600">{errors.relativePhone.message}</p>
              )}
            </div>
          </div>

          {/* Form Controls */}
          <div className="flex gap-4 pt-6 border-t">
            <Button type="submit" className="flex-1 h-12 text-base">
              <Save className="w-5 h-5 mr-2" />
              Save Patient
            </Button>
            <Button type="button" variant="outline" onClick={handleReset} className="flex-1 h-12 text-base">
              <RotateCcw className="w-5 h-5 mr-2" />
              Reset Form
            </Button>
            <Button type="button" variant="ghost" onClick={onClose} className="h-12 text-base">
              <X className="w-5 h-5 mr-2" />
              Cancel
            </Button>
          </div>
        </form>
      </DialogContent>
    </Dialog>
  );
}