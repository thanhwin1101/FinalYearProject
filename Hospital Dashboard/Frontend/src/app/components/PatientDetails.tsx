import { useState } from 'react';
import { X, Pill, AlertTriangle, FileText, Plus, Trash2, Save, CalendarIcon } from 'lucide-react';
import DatePicker from 'react-datepicker';
import 'react-datepicker/dist/react-datepicker.css';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/app/components/ui/dialog';
import { Button } from '@/app/components/ui/button';
import { Badge } from '@/app/components/ui/badge';
import { Input } from '@/app/components/ui/input';
import { Label } from '@/app/components/ui/label';
import { Patient, MedicationEntry } from '@/app/types/patient';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/app/components/ui/tabs';

interface PatientDetailsProps {
  patient: Patient | null;
  isOpen: boolean;
  onClose: () => void;
  onUpdatePatient?: (patient: Patient) => void;
}

const getStatusColor = (status: string) => {
  switch (status) {
    case 'Critical': return 'bg-[#DC3545] text-white';
    case 'Stable': return 'bg-[#28A745] text-white';
    case 'Recovering': return 'bg-[#0277BD] text-white';
    case 'Under Observation': return 'bg-[#FFC107] text-[#212529]';
    default: return 'bg-[#6C757D] text-white';
  }
};

export function PatientDetails({ patient, isOpen, onClose, onUpdatePatient }: PatientDetailsProps) {
  const [showMedForm, setShowMedForm] = useState(false);
  const [showAllergenForm, setShowAllergenForm] = useState(false);
  const [editingNotes, setEditingNotes] = useState(false);
  const [notes, setNotes] = useState(patient?.notes || '');
  const [newAllergen, setNewAllergen] = useState('');
  const [newMed, setNewMed] = useState({
    name: '',
    dosage: '',
    frequency: '',
    prescribedBy: '',
    startDate: new Date().toISOString().split('T')[0],
    notes: ''
  });

  if (!patient) return null;

  const handleAddMedication = () => {
    if (!newMed.name || !newMed.dosage) return;
    
    const medication: MedicationEntry = {
      id: crypto.randomUUID(),
      ...newMed
    };
    
    const updatedPatient = {
      ...patient,
      medicationLog: [...patient.medicationLog, medication]
    };
    
    onUpdatePatient?.(updatedPatient);
    setNewMed({ name: '', dosage: '', frequency: '', prescribedBy: '', startDate: new Date().toISOString().split('T')[0], notes: '' });
    setShowMedForm(false);
  };

  const handleDeleteMedication = (medId: string) => {
    const updatedPatient = {
      ...patient,
      medicationLog: patient.medicationLog.filter(m => m.id !== medId)
    };
    onUpdatePatient?.(updatedPatient);
  };

  const handleAddAllergen = () => {
    if (!newAllergen.trim()) return;
    
    const updatedPatient = {
      ...patient,
      allergens: [...patient.allergens, newAllergen.trim()]
    };
    
    onUpdatePatient?.(updatedPatient);
    setNewAllergen('');
    setShowAllergenForm(false);
  };

  const handleDeleteAllergen = (index: number) => {
    const updatedPatient = {
      ...patient,
      allergens: patient.allergens.filter((_, i) => i !== index)
    };
    onUpdatePatient?.(updatedPatient);
  };

  const handleSaveNotes = () => {
    const updatedPatient = {
      ...patient,
      notes
    };
    onUpdatePatient?.(updatedPatient);
    setEditingNotes(false);
  };

  return (
    <Dialog open={isOpen} onOpenChange={onClose}>
      <DialogContent className="max-w-5xl w-[90vw] h-[85vh] flex flex-col overflow-hidden">
        <DialogHeader className="flex-shrink-0">
          <DialogTitle className="text-xl font-semibold">Patient Medical Details</DialogTitle>
        </DialogHeader>

        {/* Patient Header */}
        <div className="flex items-center gap-4 p-4 bg-blue-50 rounded-lg flex-shrink-0">
          <div className="w-14 h-14 rounded-full bg-gray-200 flex items-center justify-center overflow-hidden">
            {patient.photo ? (
              <img src={patient.photo} alt={patient.fullName} className="w-full h-full object-cover" />
            ) : (
              <span className="text-xl font-semibold text-gray-600">
                {patient.fullName.charAt(0)}
              </span>
            )}
          </div>
          <div className="flex-1">
            <h3 className="text-lg font-semibold">{patient.fullName}</h3>
            <div className="flex flex-wrap gap-3 text-sm text-gray-600 mt-1">
              <span>MRN: {patient.mrn}</span>
              <span>•</span>
              <span>Bed: {patient.roomBedId}</span>
              <span>•</span>
              <span>Card: {patient.cardNumber}</span>
            </div>
          </div>
          <Badge className={getStatusColor(patient.status)}>
            {patient.status}
          </Badge>
        </div>

        {/* Tabs */}
        <Tabs defaultValue="medications" className="w-full flex-1 flex flex-col min-h-0">
          <TabsList className="grid w-full grid-cols-3 flex-shrink-0">
            <TabsTrigger value="medications" className="text-sm">
              <Pill className="w-4 h-4 mr-2" />
              Medications
            </TabsTrigger>
            <TabsTrigger value="allergens" className="text-sm">
              <AlertTriangle className="w-4 h-4 mr-2" />
              Allergens
            </TabsTrigger>
            <TabsTrigger value="notes" className="text-sm">
              <FileText className="w-4 h-4 mr-2" />
              Notes
            </TabsTrigger>
          </TabsList>

          {/* Medication Log */}
          <TabsContent value="medications" className="flex-1 overflow-y-auto space-y-4 pr-2">
            <div className="flex justify-between items-center">
              <h3 className="text-base font-semibold">Medication Log</h3>
              <Button size="sm" variant="outline" onClick={() => setShowMedForm(!showMedForm)}>
                <Plus className="w-4 h-4 mr-2" />
                Add Medication
              </Button>
            </div>

            {/* Add Medication Form */}
            {showMedForm && (
              <div className="p-4 border rounded-lg bg-gray-50 space-y-3">
                <h4 className="font-medium text-sm">New Medication</h4>
                <div className="grid grid-cols-3 gap-3">
                  <div className="space-y-1">
                    <Label className="text-sm">Medication Name *</Label>
                    <Input
                      value={newMed.name}
                      onChange={(e) => setNewMed({ ...newMed, name: e.target.value })}
                      placeholder="e.g., Aspirin"
                      className="h-9 text-sm"
                    />
                  </div>
                  <div className="space-y-1">
                    <Label className="text-sm">Dosage *</Label>
                    <Input
                      value={newMed.dosage}
                      onChange={(e) => setNewMed({ ...newMed, dosage: e.target.value })}
                      placeholder="e.g., 100mg"
                      className="h-9 text-sm"
                    />
                  </div>
                  <div className="space-y-1">
                    <Label className="text-sm">Frequency</Label>
                    <Input
                      value={newMed.frequency}
                      onChange={(e) => setNewMed({ ...newMed, frequency: e.target.value })}
                      placeholder="e.g., Once daily"
                      className="h-9 text-sm"
                    />
                  </div>
                  <div className="space-y-1">
                    <Label className="text-sm">Prescribed By</Label>
                    <Input
                      value={newMed.prescribedBy}
                      onChange={(e) => setNewMed({ ...newMed, prescribedBy: e.target.value })}
                      placeholder="e.g., Dr. Smith"
                      className="h-9 text-sm"
                    />
                  </div>
                  <div className="space-y-1">
                    <Label className="text-sm">Start Date</Label>
                    <div className="relative">
                      <DatePicker
                        selected={newMed.startDate ? new Date(newMed.startDate) : null}
                        onChange={(date: Date | null) => setNewMed({ ...newMed, startDate: date ? date.toISOString().split('T')[0] : '' })}
                        dateFormat="dd/MM/yyyy"
                        placeholderText="Select date"
                        className="h-9 text-sm w-full px-3 py-2 border border-input rounded-md bg-background cursor-pointer"
                        showPopperArrow={false}
                        popperPlacement="bottom-start"
                      />
                      <CalendarIcon className="absolute right-3 top-1/2 -translate-y-1/2 h-4 w-4 text-gray-500 pointer-events-none" />
                    </div>
                  </div>
                  <div className="space-y-1">
                    <Label className="text-sm">Notes</Label>
                    <Input
                      value={newMed.notes}
                      onChange={(e) => setNewMed({ ...newMed, notes: e.target.value })}
                      placeholder="Optional notes"
                      className="h-9 text-sm"
                    />
                  </div>
                </div>
                <div className="flex gap-2 pt-2">
                  <Button size="sm" onClick={handleAddMedication}>
                    <Save className="w-4 h-4 mr-2" />
                    Save
                  </Button>
                  <Button size="sm" variant="outline" onClick={() => setShowMedForm(false)}>
                    Cancel
                  </Button>
                </div>
              </div>
            )}

            {patient.medicationLog.length === 0 ? (
              <div className="text-center py-8 text-gray-500 text-sm">
                No medications recorded
              </div>
            ) : (
              <div className="space-y-3">
                {patient.medicationLog.map((med) => (
                  <div key={med.id} className="p-4 border rounded-lg hover:bg-gray-50">
                    <div className="flex justify-between items-start">
                      <div className="flex-1">
                        <h4 className="font-semibold text-base">{med.name}</h4>
                        <div className="grid grid-cols-2 gap-2 mt-2 text-sm">
                          <div>
                            <span className="text-gray-600">Dosage:</span>
                            <span className="ml-2 font-medium">{med.dosage}</span>
                          </div>
                          <div>
                            <span className="text-gray-600">Frequency:</span>
                            <span className="ml-2 font-medium">{med.frequency}</span>
                          </div>
                          <div>
                            <span className="text-gray-600">Prescribed by:</span>
                            <span className="ml-2 font-medium">{med.prescribedBy}</span>
                          </div>
                          <div>
                            <span className="text-gray-600">Start Date:</span>
                            <span className="ml-2 font-medium">
                              {new Date(med.startDate).toLocaleDateString()}
                            </span>
                          </div>
                        </div>
                        {med.notes && (
                          <div className="mt-2 text-sm text-gray-600 italic">
                            Note: {med.notes}
                          </div>
                        )}
                      </div>
                      <Button variant="ghost" size="sm" onClick={() => handleDeleteMedication(med.id)}>
                        <Trash2 className="w-4 h-4 text-red-600" />
                      </Button>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </TabsContent>

          {/* Allergens */}
          <TabsContent value="allergens" className="flex-1 overflow-y-auto space-y-4 pr-2">
            <div className="flex justify-between items-center">
              <h3 className="text-base font-semibold">Known Allergens</h3>
              <Button size="sm" variant="outline" onClick={() => setShowAllergenForm(!showAllergenForm)}>
                <Plus className="w-4 h-4 mr-2" />
                Add Allergen
              </Button>
            </div>

            {/* Add Allergen Form */}
            {showAllergenForm && (
              <div className="p-4 border rounded-lg bg-gray-50 space-y-3">
                <h4 className="font-medium text-sm">New Allergen</h4>
                <div className="flex gap-2">
                  <Input
                    value={newAllergen}
                    onChange={(e) => setNewAllergen(e.target.value)}
                    placeholder="e.g., Penicillin, Peanuts, Latex..."
                    className="flex-1 h-9 text-sm"
                  />
                  <Button size="sm" onClick={handleAddAllergen}>
                    <Save className="w-4 h-4 mr-2" />
                    Save
                  </Button>
                  <Button size="sm" variant="outline" onClick={() => setShowAllergenForm(false)}>
                    Cancel
                  </Button>
                </div>
              </div>
            )}

            {patient.allergens.length === 0 ? (
              <div className="text-center py-8 text-gray-500 text-sm">
                No allergens recorded
              </div>
            ) : (
              <div className="space-y-2">
                {patient.allergens.map((allergen, index) => (
                  <div
                    key={index}
                    className="flex items-center justify-between p-3 bg-red-50 border border-red-200 rounded-lg"
                  >
                    <div className="flex items-center gap-2">
                      <AlertTriangle className="w-5 h-5 text-red-600" />
                      <span className="font-medium text-sm text-red-900">{allergen}</span>
                    </div>
                    <Button variant="ghost" size="sm" onClick={() => handleDeleteAllergen(index)}>
                      <Trash2 className="w-4 h-4 text-red-600" />
                    </Button>
                  </div>
                ))}
              </div>
            )}

            <div className="p-3 bg-yellow-50 rounded-lg border border-yellow-200">
              <p className="text-sm text-yellow-800">
                <strong>Important:</strong> Always verify allergen information before administering any medication or treatment.
              </p>
            </div>
          </TabsContent>

          {/* Notes */}
          <TabsContent value="notes" className="flex-1 overflow-y-auto space-y-4 pr-2">
            <div className="flex justify-between items-center">
              <h3 className="text-base font-semibold">Patient Notes</h3>
              {!editingNotes && (
                <Button size="sm" variant="outline" onClick={() => { setNotes(patient.notes || ''); setEditingNotes(true); }}>
                  <FileText className="w-4 h-4 mr-2" />
                  Edit Notes
                </Button>
              )}
            </div>
            
            {editingNotes ? (
              <div className="space-y-3">
                <textarea
                  value={notes}
                  onChange={(e) => setNotes(e.target.value)}
                  placeholder="Enter patient notes here..."
                  className="w-full h-40 p-3 border rounded-lg text-sm resize-none focus:outline-none focus:ring-2 focus:ring-blue-500"
                />
                <div className="flex gap-2">
                  <Button size="sm" onClick={handleSaveNotes}>
                    <Save className="w-4 h-4 mr-2" />
                    Save Notes
                  </Button>
                  <Button size="sm" variant="outline" onClick={() => setEditingNotes(false)}>
                    Cancel
                  </Button>
                </div>
              </div>
            ) : patient.notes ? (
              <div className="p-4 bg-gray-50 rounded-lg border flex-1">
                <p className="whitespace-pre-wrap text-sm">{patient.notes}</p>
              </div>
            ) : (
              <div className="text-center py-8 text-gray-500 text-sm flex-1 flex items-center justify-center">
                No notes recorded
              </div>
            )}
          </TabsContent>
        </Tabs>

        <div className="flex justify-end pt-4 border-t flex-shrink-0">
          <Button onClick={onClose}>
            <X className="w-4 h-4 mr-2" />
            Close
          </Button>
        </div>
      </DialogContent>
    </Dialog>
  );
}