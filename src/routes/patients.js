import { Router } from 'express';
import multer from 'multer';
import fs from 'fs/promises';
import path from 'path';
import Patient from '../models/Patient.js';

const r = Router();

// =========================
// Upload config
// =========================
const UPLOAD_DIR = path.join(process.cwd(), 'uploads', 'patients');

async function ensureUploadDir() {
  try { await fs.mkdir(UPLOAD_DIR, { recursive: true }); } catch {}
}

const storage = multer.diskStorage({
  destination: async (_req, _file, cb) => {
    try { await ensureUploadDir(); } catch {}
    cb(null, UPLOAD_DIR);
  },
  filename: (_req, file, cb) => {
    const ext = (path.extname(file.originalname) || '.jpg').toLowerCase();
    const safeExt = ['.jpg', '.jpeg', '.png', '.webp'].includes(ext) ? ext : '.jpg';
    cb(null, `p_${Date.now()}_${Math.random().toString(16).slice(2)}${safeExt}`);
  }
});

function fileFilter(_req, file, cb) {
  if (String(file.mimetype || '').startsWith('image/')) return cb(null, true);
  cb(new Error('Only image files are allowed'), false);
}

const upload = multer({
  storage,
  fileFilter,
  limits: { fileSize: 5 * 1024 * 1024 } // 5MB
});

// =========================
// Helpers
// =========================
function norm(s='') {
  return String(s).toLowerCase().replace(/\s+/g,'').replace(/[^a-z0-9]/g,'');
}

function toAbsPathFromPhoto(photoPathOrUrl) {
  if (!photoPathOrUrl) return null;

  // trường hợp lưu /uploads/...
  if (photoPathOrUrl.startsWith('/')) {
    const rel = photoPathOrUrl.replace(/^\//, ''); // uploads/patients/xxx.jpg
    return path.join(process.cwd(), rel);
  }

  // trường hợp lưu uploads/patients/...
  return path.join(process.cwd(), photoPathOrUrl);
}

async function safeUnlink(filePathOrUrl) {
  const abs = toAbsPathFromPhoto(filePathOrUrl);
  if (!abs) return;

  try {
    await fs.unlink(abs);
  } catch (e) {
    // nếu file không tồn tại thì bỏ qua, tránh crash
    if (e && e.code === 'ENOENT') return;
    console.log('[UNLINK ERROR]', e?.message || e);
  }
}

function buildPhotoFieldsFromMulterFile(file) {
  // file.path: .../uploads/patients/xxx.jpg (absolute depending on multer)
  // Ta muốn lưu relative + url
  const filename = file.filename;
  const relPath = path.join('uploads', 'patients', filename).replaceAll('\\', '/');
  const urlPath = '/' + relPath.replaceAll('\\', '/');
  return { photoPath: relPath, photoUrl: urlPath };
}

function parseDateOnly(s) {
  const v = String(s || '').slice(0, 10);
  return /^\d{4}-\d{2}-\d{2}$/.test(v) ? v : '';
}

// =========================
// META (filters options)
// =========================
r.get('/meta', async (_req, res) => {
  const statuses = ['Stable', 'Under Observation', 'Critical', 'Discharged'];

  const doctors = await Patient.distinct('primaryDoctor');
  const departments = await Patient.distinct('department');

  res.json({
    statuses,
    doctors: doctors.filter(Boolean).sort(),
    departments: departments.filter(Boolean).sort()
  });
});

// =========================
// LIST with filters
// =========================
r.get('/', async (req, res) => {
  try {
    const { q, status, doctor, department, from, to, sort } = req.query;

    const filter = {};

    if (status) filter.status = String(status);
    if (doctor) filter.primaryDoctor = String(doctor);
    if (department) filter.department = String(department);

    // admissionDate range
    const fromD = parseDateOnly(from);
    const toD = parseDateOnly(to);
    if (fromD || toD) {
      filter.admissionDate = {};
      if (fromD) filter.admissionDate.$gte = fromD;
      if (toD) filter.admissionDate.$lte = toD;
    }

    let list = await Patient.find(filter).lean();

    // quick search client-side (đủ nhanh cho demo)
    if (q && String(q).trim()) {
      const target = norm(q);
      list = list.filter(p => {
        const bag = [
          p.fullName, p.mrn, p.cardNumber, p.primaryDoctor,
          p.relativeName, p.relativePhone, p.department
        ].map(norm).join('|');
        return bag.includes(target);
      });
    }

    // sort
    const s = String(sort || 'newestAdmission');
    if (s === 'oldestAdmission') list.sort((a,b)=> String(a.admissionDate).localeCompare(String(b.admissionDate)));
    else if (s === 'nameAZ') list.sort((a,b)=> String(a.fullName).localeCompare(String(b.fullName)));
    else if (s === 'nameZA') list.sort((a,b)=> String(b.fullName).localeCompare(String(a.fullName)));
    else list.sort((a,b)=> String(b.admissionDate).localeCompare(String(a.admissionDate)));

    res.json(list);
  } catch (e) {
    res.status(500).json({ message: e.message || 'Load failed' });
  }
});

// =========================
// MRN generate
// =========================
r.get('/mrn/generate', async (_req, res) => {
  const year = new Date().getFullYear();
  for (let i = 0; i < 30; i++) {
    const suffix = String(Math.floor(Math.random() * 900) + 100);
    const mrn = `MRN-${year}-${suffix}`;
    const exists = await Patient.exists({ mrn });
    if (!exists) return res.json({ mrn });
  }
  res.status(500).json({ message: 'Cannot generate MRN now' });
});

// =========================
// CREATE (multipart)
// =========================
r.post('/', upload.single('photo'), async (req, res) => {
  try {
    const body = req.body || {};

    const doc = {
      fullName: String(body.fullName || '').trim(),
      mrn: String(body.mrn || '').trim(),
      dob: body.dob ? String(body.dob) : undefined,
      gender: body.gender ? String(body.gender) : undefined,
      admissionDate: String(body.admissionDate || '').trim(),
      status: String(body.status || '').trim(),
      department: body.department ? String(body.department).trim() : undefined,
      ward: body.ward ? String(body.ward).trim() : undefined,
      roomBed: body.roomBed ? String(body.roomBed).trim() : undefined,
      primaryDoctor: String(body.primaryDoctor || '').trim(),
      cardNumber: String(body.cardNumber || '').trim(),
      relativeName: String(body.relativeName || '').trim(),
      relativePhone: String(body.relativePhone || '').trim(),
      insurancePolicyId: body.insurancePolicyId ? String(body.insurancePolicyId).trim() : undefined
    };

    // required validation
    const required = ['fullName','mrn','admissionDate','status','primaryDoctor','cardNumber','relativeName','relativePhone'];
    for (const k of required) {
      if (!doc[k]) return res.status(400).json({ message: `${k} is required` });
    }

    if (req.file) Object.assign(doc, buildPhotoFieldsFromMulterFile(req.file));

    const created = await Patient.create(doc);
    res.status(201).json(created);
  } catch (e) {
    res.status(500).json({ message: e.message || 'Create failed' });
  }
});

// =========================
// UPDATE (multipart)
// - nếu upload ảnh mới => xóa ảnh cũ
// =========================
r.put('/:id', upload.single('photo'), async (req, res) => {
  try {
    const id = req.params.id;
    const patient = await Patient.findById(id);
    if (!patient) return res.status(404).json({ message: 'Patient not found' });

    const body = req.body || {};

    // nếu có ảnh mới => xóa ảnh cũ trước
    if (req.file) {
      await safeUnlink(patient.photoPath || patient.photoUrl);
      const photo = buildPhotoFieldsFromMulterFile(req.file);
      patient.photoPath = photo.photoPath;
      patient.photoUrl = photo.photoUrl;
    }

    // update fields
    const assignIf = (key, val) => { if (val !== undefined) patient[key] = val; };

    assignIf('fullName', body.fullName ? String(body.fullName).trim() : undefined);
    assignIf('mrn', body.mrn ? String(body.mrn).trim() : undefined);
    assignIf('dob', body.dob ? String(body.dob) : undefined);
    assignIf('gender', body.gender ? String(body.gender) : undefined);
    assignIf('admissionDate', body.admissionDate ? String(body.admissionDate).trim() : undefined);
    assignIf('status', body.status ? String(body.status).trim() : undefined);
    assignIf('department', body.department ? String(body.department).trim() : undefined);
    assignIf('ward', body.ward ? String(body.ward).trim() : undefined);
    assignIf('roomBed', body.roomBed ? String(body.roomBed).trim() : undefined);
    assignIf('primaryDoctor', body.primaryDoctor ? String(body.primaryDoctor).trim() : undefined);
    assignIf('cardNumber', body.cardNumber ? String(body.cardNumber).trim() : undefined);
    assignIf('relativeName', body.relativeName ? String(body.relativeName).trim() : undefined);
    assignIf('relativePhone', body.relativePhone ? String(body.relativePhone).trim() : undefined);
    assignIf('insurancePolicyId', body.insurancePolicyId ? String(body.insurancePolicyId).trim() : undefined);

    await patient.save();
    res.json(patient);
  } catch (e) {
    res.status(500).json({ message: e.message || 'Update failed' });
  }
});

// =========================
// ✅ DELETE patient
// - xóa doc + unlink ảnh trong uploads
// =========================
r.delete('/:id', async (req, res) => {
  try {
    const id = req.params.id;
    const patient = await Patient.findById(id);
    if (!patient) return res.status(404).json({ message: 'Patient not found' });

    // ✅ 1) xóa ảnh vật lý trước
    await safeUnlink(patient.photoPath || patient.photoUrl);

    // ✅ 2) xóa document
    await Patient.deleteOne({ _id: id });

    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Delete failed' });
  }
});

// =========================
// DETAILS bundle
// =========================
r.get('/:id/details', async (req, res) => {
  try {
    const p = await Patient.findById(req.params.id).lean();
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    res.json({
      patient: p,
      timeline: p.timeline || [],
      prescriptions: p.prescriptions || [],
      notes: p.notes || []
    });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Load details failed' });
  }
});

// Timeline
r.post('/:id/timeline', async (req, res) => {
  try {
    const { title, description, createdBy, at } = req.body || {};
    if (!title) return res.status(400).json({ message: 'title is required' });

    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.timeline.push({
      title: String(title).trim(),
      description: description ? String(description).trim() : undefined,
      createdBy: createdBy ? String(createdBy).trim() : undefined,
      at: at ? new Date(at) : new Date()
    });

    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Add timeline failed' });
  }
});

r.delete('/:id/timeline/:tid', async (req, res) => {
  try {
    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.timeline = (p.timeline || []).filter(x => String(x._id) !== String(req.params.tid));
    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Delete timeline failed' });
  }
});

// Prescriptions
r.post('/:id/prescriptions', async (req, res) => {
  try {
    const b = req.body || {};
    const required = ['medication','dosage','frequency'];
    for (const k of required) if (!b[k]) return res.status(400).json({ message: `${k} is required` });

    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.prescriptions.push({
      medication: String(b.medication).trim(),
      dosage: String(b.dosage).trim(),
      frequency: String(b.frequency).trim(),
      startDate: b.startDate ? String(b.startDate) : undefined,
      endDate: b.endDate ? String(b.endDate) : undefined,
      prescribedBy: b.prescribedBy ? String(b.prescribedBy).trim() : undefined,
      instructions: b.instructions ? String(b.instructions).trim() : undefined,
      status: 'Active'
    });

    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Add prescription failed' });
  }
});

r.put('/:id/prescriptions/:pid', async (req, res) => {
  try {
    const { status } = req.body || {};
    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    const item = (p.prescriptions || []).find(x => String(x._id) === String(req.params.pid));
    if (!item) return res.status(404).json({ message: 'Prescription not found' });

    item.status = String(status || 'Active');
    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Update prescription failed' });
  }
});

r.delete('/:id/prescriptions/:pid', async (req, res) => {
  try {
    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.prescriptions = (p.prescriptions || []).filter(x => String(x._id) !== String(req.params.pid));
    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Delete prescription failed' });
  }
});

// Notes
r.post('/:id/notes', async (req, res) => {
  try {
    const { text, createdBy } = req.body || {};
    if (!text) return res.status(400).json({ message: 'text is required' });

    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.notes.push({
      text: String(text).trim(),
      createdBy: createdBy ? String(createdBy).trim() : undefined,
      createdAt: new Date()
    });

    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Add note failed' });
  }
});

r.delete('/:id/notes/:nid', async (req, res) => {
  try {
    const p = await Patient.findById(req.params.id);
    if (!p) return res.status(404).json({ message: 'Patient not found' });

    p.notes = (p.notes || []).filter(x => String(x._id) !== String(req.params.nid));
    await p.save();
    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Delete note failed' });
  }
});

export default r;
