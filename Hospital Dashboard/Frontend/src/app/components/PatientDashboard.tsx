import { useState, useMemo } from 'react';
import { Search, Filter, Download, UserPlus, Edit, Trash2, RotateCcw, User, ArrowUpDown, CalendarIcon } from 'lucide-react';
import DatePicker from 'react-datepicker';
import 'react-datepicker/dist/react-datepicker.css';
import { Button } from '@/app/components/ui/button';
import { Input } from '@/app/components/ui/input';
import { Label } from '@/app/components/ui/label';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/app/components/ui/table';
import { Badge } from '@/app/components/ui/badge';
import { Patient, SearchFilters, PATIENT_STATUSES } from '@/app/types/patient';
import { exportToCSV, sortPatientsByBedOrder } from '@/app/utils/patient-helpers';
import { PatientForm } from '@/app/components/PatientForm';
import { PatientDetails } from '@/app/components/PatientDetails';

type SortOption = 'bed' | 'name-asc' | 'name-desc' | 'date-newest' | 'date-oldest';

interface PatientDashboardProps {
  patients: Patient[];
  onAddPatient: (patient: Patient, photoFile?: File) => void;
  onUpdatePatient: (patient: Patient, photoFile?: File) => void;
  onDeletePatient: (id: string) => void;
}

export function PatientDashboard({ patients, onAddPatient, onUpdatePatient, onDeletePatient }: PatientDashboardProps) {
  const [filters, setFilters] = useState<SearchFilters>({
    quickSearch: '',
    status: '',
    primaryDoctor: '',
    department: '',
    dateFrom: '',
    dateTo: ''
  });

  const [isFormOpen, setIsFormOpen] = useState(false);
  const [editingPatient, setEditingPatient] = useState<Patient | null>(null);
  const [isDetailsOpen, setIsDetailsOpen] = useState(false);
  const [selectedPatient, setSelectedPatient] = useState<Patient | null>(null);
  const [sortOption, setSortOption] = useState<SortOption>('bed');

  // Get unique doctors and departments for filters
  const doctors = useMemo(() => 
    Array.from(new Set(patients.map(p => p.primaryDoctor))).filter(Boolean),
    [patients]
  );

  const departments = useMemo(() => 
    Array.from(new Set(patients.map(p => p.department))).filter(Boolean),
    [patients]
  );

  // Filter and sort patients
  const filteredPatients = useMemo(() => {
    let result = [...patients];

    // Quick search
    if (filters.quickSearch) {
      const search = filters.quickSearch.toLowerCase();
      result = result.filter(p =>
        p.fullName.toLowerCase().includes(search) ||
        p.mrn.toLowerCase().includes(search) ||
        p.cardNumber.toLowerCase().includes(search) ||
        p.primaryDoctor.toLowerCase().includes(search) ||
        p.relativeName.toLowerCase().includes(search)
      );
    }

    // Status filter
    if (filters.status) {
      result = result.filter(p => p.status === filters.status);
    }

    // Doctor filter
    if (filters.primaryDoctor) {
      result = result.filter(p => p.primaryDoctor === filters.primaryDoctor);
    }

    // Department filter
    if (filters.department) {
      result = result.filter(p => p.department === filters.department);
    }

    // Date range filter
    if (filters.dateFrom) {
      result = result.filter(p => p.admissionDate >= filters.dateFrom);
    }
    if (filters.dateTo) {
      result = result.filter(p => p.admissionDate <= filters.dateTo);
    }

    // Sort based on selected option
    switch (sortOption) {
      case 'name-asc':
        result.sort((a, b) => a.fullName.localeCompare(b.fullName));
        break;
      case 'name-desc':
        result.sort((a, b) => b.fullName.localeCompare(a.fullName));
        break;
      case 'date-newest':
        result.sort((a, b) => new Date(b.admissionDate).getTime() - new Date(a.admissionDate).getTime());
        break;
      case 'date-oldest':
        result.sort((a, b) => new Date(a.admissionDate).getTime() - new Date(b.admissionDate).getTime());
        break;
      default:
        result = sortPatientsByBedOrder(result);
    }

    return result;
  }, [patients, filters, sortOption]);

  const handleResetFilters = () => {
    setFilters({
      quickSearch: '',
      status: '',
      primaryDoctor: '',
      department: '',
      dateFrom: '',
      dateTo: ''
    });
  };

  const handleExportCSV = () => {
    exportToCSV(filteredPatients);
  };

  const handleAddPatient = () => {
    setEditingPatient(null);
    setIsFormOpen(true);
  };

  const handleEditPatient = (patient: Patient) => {
    setEditingPatient(patient);
    setIsFormOpen(true);
  };

  const handleSavePatient = (patient: Patient, photoFile?: File) => {
    if (editingPatient) {
      onUpdatePatient(patient, photoFile);
    } else {
      onAddPatient(patient, photoFile);
    }
  };

  const handleViewPatientDetails = (patient: Patient) => {
    setSelectedPatient(patient);
    setIsDetailsOpen(true);
  };

  const getStatusColor = (status: Patient['status']) => {
    const colors = {
      'Stable': 'bg-[#28A745] text-white',
      'Critical': 'bg-[#DC3545] text-white',
      'Recovering': 'bg-[#0277BD] text-white',
      'Under Observation': 'bg-[#FFC107] text-[#212529]',
      'Discharged': 'bg-[#6C757D] text-white'
    };
    return colors[status] || 'bg-gray-100 text-gray-800';
  };

  return (
    <div className="space-y-8">
      {/* Search & Filtering System */}
      <div className="bg-white p-8 rounded-xl shadow-sm border">
        <h2 className="text-2xl font-semibold mb-6">Search & Filters</h2>
        
        {/* Quick Search */}
        <div className="space-y-6">
          <div className="space-y-3">
            <Label htmlFor="quickSearch" className="text-base font-medium">Quick Search</Label>
            <div className="relative">
              <Search className="absolute left-4 top-1/2 -translate-y-1/2 w-5 h-5 text-gray-400" />
              <Input
                id="quickSearch"
                value={filters.quickSearch}
                onChange={(e) => setFilters({ ...filters, quickSearch: e.target.value })}
                placeholder="Search by Name, MRN, RFID Card, Doctor, or Relative..."
                className="pl-12 h-12 text-base"
              />
            </div>
          </div>

          {/* Advanced Filters */}
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
            <div className="space-y-3">
              <Label htmlFor="status" className="text-base font-medium">Patient Status</Label>
              <select
                id="status"
                value={filters.status}
                onChange={(e) => setFilters({ ...filters, status: e.target.value })}
                className="flex h-12 w-full rounded-md border border-input bg-background px-4 py-3 text-base"
              >
                <option value="">All Statuses</option>
                {PATIENT_STATUSES.map(status => (
                  <option key={status} value={status}>{status}</option>
                ))}
              </select>
            </div>

            <div className="space-y-3">
              <Label htmlFor="doctor" className="text-base font-medium">Primary Doctor</Label>
              <select
                id="doctor"
                value={filters.primaryDoctor}
                onChange={(e) => setFilters({ ...filters, primaryDoctor: e.target.value })}
                className="flex h-12 w-full rounded-md border border-input bg-background px-4 py-3 text-base"
              >
                <option value="">All Doctors</option>
                {doctors.map(doctor => (
                  <option key={doctor} value={doctor}>{doctor}</option>
                ))}
              </select>
            </div>

            <div className="space-y-3">
              <Label htmlFor="department" className="text-base font-medium">Department</Label>
              <select
                id="department"
                value={filters.department}
                onChange={(e) => setFilters({ ...filters, department: e.target.value })}
                className="flex h-12 w-full rounded-md border border-input bg-background px-4 py-3 text-base"
              >
                <option value="">All Departments</option>
                {departments.map(dept => (
                  <option key={dept} value={dept}>{dept}</option>
                ))}
              </select>
            </div>
          </div>

          {/* Date Range and Action Buttons */}
          <div className="flex items-end justify-between">
            {/* Date Range - Left side */}
            <div className="flex gap-3 items-end">
              <div className="space-y-1">
                <Label className="text-sm font-medium">From</Label>
                <div className="relative">
                  <DatePicker
                    selected={filters.dateFrom ? new Date(filters.dateFrom) : null}
                    onChange={(date: Date | null) => setFilters({ ...filters, dateFrom: date ? date.toISOString().split('T')[0] : '' })}
                    dateFormat="dd/MM/yyyy"
                    placeholderText="Select date"
                    className="h-10 text-base w-44 px-3 py-2 border border-input rounded-md bg-background cursor-pointer"
                    calendarClassName="!text-lg"
                    showPopperArrow={false}
                    popperPlacement="bottom-start"
                  />
                  <CalendarIcon className="absolute right-3 top-1/2 -translate-y-1/2 h-4 w-4 text-gray-500 pointer-events-none" />
                </div>
              </div>
              <div className="space-y-1">
                <Label className="text-sm font-medium">To</Label>
                <div className="relative">
                  <DatePicker
                    selected={filters.dateTo ? new Date(filters.dateTo) : null}
                    onChange={(date: Date | null) => setFilters({ ...filters, dateTo: date ? date.toISOString().split('T')[0] : '' })}
                    dateFormat="dd/MM/yyyy"
                    placeholderText="Select date"
                    className="h-10 text-base w-44 px-3 py-2 border border-input rounded-md bg-background cursor-pointer"
                    calendarClassName="!text-lg"
                    showPopperArrow={false}
                    popperPlacement="bottom-start"
                  />
                  <CalendarIcon className="absolute right-3 top-1/2 -translate-y-1/2 h-4 w-4 text-gray-500 pointer-events-none" />
                </div>
              </div>
            </div>

            {/* Action Buttons - Right side */}
            <div className="flex gap-3 items-center">
              <Button onClick={handleResetFilters} variant="outline" size="default" className="text-sm">
                <RotateCcw className="w-4 h-4 mr-2" />
                Reset Filters
              </Button>
              <Button onClick={handleExportCSV} variant="outline" size="default" className="text-sm">
                <Download className="w-4 h-4 mr-2" />
                Export CSV
              </Button>
              <Button onClick={handleAddPatient} size="default" className="text-sm">
                <UserPlus className="w-4 h-4 mr-2" />
                Add Patient
              </Button>
            </div>
          </div>
        </div>
      </div>

      {/* Patient List Display */}
      <div className="bg-white rounded-xl shadow-sm border">
        <div className="p-4 border-b flex items-center justify-between">
          <div>
            <h2 className="text-xl font-semibold">
              Patient List ({filteredPatients.length} {filteredPatients.length === 1 ? 'patient' : 'patients'})
            </h2>
          </div>
          <div className="flex items-center gap-2">
            <ArrowUpDown className="w-4 h-4 text-gray-500" />
            <select
              value={sortOption}
              onChange={(e) => setSortOption(e.target.value as SortOption)}
              className="h-9 px-3 rounded-md border border-input bg-background text-sm"
            >
              <option value="bed">Sort by Bed Order</option>
              <option value="name-asc">Name A-Z</option>
              <option value="name-desc">Name Z-A</option>
              <option value="date-newest">Newest Admission</option>
              <option value="date-oldest">Oldest Admission</option>
            </select>
          </div>
        </div>
        
        <div className="overflow-x-auto">
          <Table className="table-fixed w-full patient-list-table">
            <TableHeader>
              <TableRow>
                <TableHead className="text-base font-semibold py-3 w-[40px]">#</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[60px]">Photo</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[120px]">Name</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[100px]">MRN</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[90px]">Admission</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[60px]">Bed</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[100px]">Status</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[110px]">Doctor</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[100px]">RFID Card</TableHead>
                <TableHead className="text-base font-semibold py-3 w-[120px]">Relative</TableHead>
                <TableHead className="text-base font-semibold py-3 text-center w-[110px]">Actions</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {filteredPatients.length === 0 ? (
                <TableRow>
                  <TableCell colSpan={11} className="text-center py-10 text-gray-500 text-base">
                    No patients found. Click "Add Patient" to register a new patient.
                  </TableCell>
                </TableRow>
              ) : (
                filteredPatients.map((patient, index) => (
                  <TableRow key={patient.id} className="cursor-pointer hover:bg-gray-50 h-16">
                    <TableCell className="text-base font-medium py-3 text-center">{index + 1}</TableCell>
                    <TableCell onClick={() => handleViewPatientDetails(patient)} className="py-3">
                      <div className="w-12 h-12 rounded-full bg-gray-100 flex items-center justify-center overflow-hidden">
                        {patient.photo ? (
                          <img src={patient.photo} alt={patient.fullName} className="w-full h-full object-cover" />
                        ) : (
                          <User className="w-6 h-6 text-gray-400" />
                        )}
                      </div>
                    </TableCell>
                    <TableCell className="font-medium text-base py-3" onClick={() => handleViewPatientDetails(patient)}>{patient.fullName}</TableCell>
                    <TableCell className="font-mono text-base py-3" onClick={() => handleViewPatientDetails(patient)}>{patient.mrn}</TableCell>
                    <TableCell className="text-base py-3" onClick={() => handleViewPatientDetails(patient)}>{new Date(patient.admissionDate).toLocaleDateString()}</TableCell>
                    <TableCell onClick={() => handleViewPatientDetails(patient)} className="py-3">
                      <Badge variant="outline" className="text-sm px-2 py-0.5">{patient.roomBedId}</Badge>
                    </TableCell>
                    <TableCell onClick={() => handleViewPatientDetails(patient)} className="py-3">
                      <Badge className={`${getStatusColor(patient.status)} text-sm px-2 py-0.5`}>{patient.status}</Badge>
                    </TableCell>
                    <TableCell className="text-base py-3" onClick={() => handleViewPatientDetails(patient)}>{patient.primaryDoctor}</TableCell>
                    <TableCell className="font-mono text-base py-3" onClick={() => handleViewPatientDetails(patient)}>{patient.cardNumber}</TableCell>
                    <TableCell onClick={() => handleViewPatientDetails(patient)} className="py-3">
                      <div className="text-base">
                        <div>{patient.relativeName}</div>
                        <div className="text-gray-500 text-sm">{patient.relativePhone}</div>
                      </div>
                    </TableCell>
                    <TableCell className="py-3">
                      <div className="flex justify-center gap-1">
                        <Button
                          variant="ghost"
                          size="icon"
                          onClick={() => handleEditPatient(patient)}
                          className="h-10 w-10"
                        >
                          <Edit className="w-8 h-8" />
                        </Button>
                        <Button
                          variant="ghost"
                          size="icon"
                          onClick={() => {
                            if (confirm(`Are you sure you want to delete ${patient.fullName}?`)) {
                              onDeletePatient(patient.id);
                            }
                          }}
                          className="h-10 w-10"
                        >
                          <Trash2 className="w-8 h-8 text-red-600" />
                        </Button>
                        <Button
                          variant="ghost"
                          size="icon"
                          onClick={() => handleViewPatientDetails(patient)}
                          className="h-10 w-10"
                        >
                          <User className="w-8 h-8" />
                        </Button>
                      </div>
                    </TableCell>
                  </TableRow>
                ))
              )}
            </TableBody>
          </Table>
        </div>
      </div>

      {/* Patient Form Dialog */}
      <PatientForm
        isOpen={isFormOpen}
        onClose={() => {
          setIsFormOpen(false);
          setEditingPatient(null);
        }}
        onSave={handleSavePatient}
        editingPatient={editingPatient}
        existingPatients={patients}
      />

      {/* Patient Details Dialog */}
      <PatientDetails
        patient={selectedPatient}
        isOpen={isDetailsOpen}
        onClose={() => {
          setIsDetailsOpen(false);
          setSelectedPatient(null);
        }}
        onUpdatePatient={onUpdatePatient}
      />
    </div>
  );
}