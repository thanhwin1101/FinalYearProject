import { Router } from 'express';
import multer from 'multer';
import fs from 'fs/promises';
import path from 'path';
import Patient from '../models/Patient.js';
import MapGraph from '../models/MapGraph.js';

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
  if (photoPathOrUrl.startsWith('/')) {
    const rel = photoPathOrUrl.replace(/^\//, '');
    return path.join(process.cwd(), rel);
  }
  return path.join(process.cwd(), photoPathOrUrl);
}

async function safeUnlink(filePathOrUrl) {
  const abs = toAbsPathFromPhoto(filePathOrUrl);
  if (!abs) return;
  try {
    await fs.unlink(abs);
  } catch (e) {
    if (e && e.code === 'ENOENT') return;
    console.log('[UNLINK ERROR]', e?.message || e);
  }
}

function buildPhotoFieldsFromMulterFile(file) {
  const filename = file.filename;
  const relPath = path.join('uploads', 'patients', filename).replaceAll('\\', '/');
  const urlPath = '/' + relPath.replaceAll('\\', '/');
  return { photoPath: relPath, photoUrl: urlPath };
}

function parseDateOnly(s) {
  const v = String(s || '').slice(0, 10);
  return /^\d{4}-\d{2}-\d{2}$/.test(v) ? v : '';
}

function parseBedKeyAny(bedId='') {
  const canonical = String(bedId).match(/^R(\d+)([MO])(\d+)$/i);
  if (canonical) {
    return { room: Number(canonical[1]), bed: Number(canonical[3]) };
  }
  const legacy = String(bedId).match(/^R(\d+)-Bed(\d+)$/i);
  if (legacy) {
    return { room: Number(legacy[1]), bed: Number(legacy[2]) };
  }
  return { room: 9999, bed: 9999 };
}

function canonicalToLegacy(canonicalId) {
  const m = String(canonicalId).match(/^R(\d+)([MO])(\d+)$/i);
  if (!m) return null;
  const room = Number(m[1]);
  const side = m[2].toUpperCase();
  const idx = Number(m[3]);
  const bedMap = { M1: 1, O1: 2, M2: 3, O2: 4, M3: 5, O3: 6 };
  const bedNum = bedMap[`${side}${idx}`];
  if (!bedNum) return null;
  return `R${room}-Bed${bedNum}`;
}

function getBedsAliases(bedId) {
  const aliases = [String(bedId).toUpperCase()];
  const canonical = String(bedId).match(/^R(\d+)([MO])(\d+)$/i);
  const legacy = String(bedId).match(/^R(\d+)-Bed(\d+)$/i);
  
  if (canonical) {
    const leg = canonicalToLegacy(bedId);
    if (leg) aliases.push(leg.toUpperCase());
  } else if (legacy) {
    const room = legacy[1];
    const bed = Number(legacy[2]);
    const bedMap = { 1: 'M1', 2: 'O1', 3: 'M2', 4: 'O2', 5: 'M3', 6: 'O3' };
    const can = `R${room}${bedMap[bed]}`;
    if (can) aliases.push(can.toUpperCase());
  }
  
  return aliases;
}

function normalizeBedToCanonical(bedId) {
  const canonical = String(bedId).match(/^R(\d+)([MO])(\d+)$/i);
  const legacy = String(bedId).match(/^R(\d+)-Bed(\d+)$/i);
  
  if (canonical) {
    return `R${canonical[1]}${canonical[2].toUpperCase()}${canonical[3]}`;
  }
  
  if (legacy) {
    const room = legacy[1];
    const bed = Number(legacy[2]);
    const bedMap = { 1: 'M1', 2: 'O1', 3: 'M2', 4: 'O2', 5: 'M3', 6: 'O3' };
    const can = `R${room}${bedMap[bed]}`;
    return can || null;
  }
  
  return null;
}

function fallbackBedIds() {
  const out = [];
  const bedMap = { 1: 'M1', 2: 'O1', 3: 'M2', 4: 'O2', 5: 'M3', 6: 'O3' };
  for (let r = 1; r <= 4; r++) {
    for (let b = 1; b <= 6; b++) out.push(`R${r}${bedMap[b]}`);
  }
  return out;
}

async function getValidBedIds(mapId='F1') {
  const map = await MapGraph.findOne({ mapId: String(mapId) }).lean().catch(()=>null);
  const beds = (map?.nodes || [])
    .filter(n => n.kind === 'bed' && n.bedId)
    .map(n => String(n.bedId));

  const list = beds.length ? beds : fallbackBedIds();

  list.sort((a,b) => {
    const A = parseBedKeyAny(a);
    const B = parseBedKeyAny(b);
    if (A.room !== B.room) return A.room - B.room;
    return A.bed - B.bed;
  });

  return list;
}

async function isValidBed(bedId) {
  const validBeds = await getValidBedIds('F1');
  const aliases = getBedsAliases(bedId);
  return validBeds.some(b => aliases.includes(b.toUpperCase()));
}

function toPatientSummary(p) {
  if (!p) return null;
  return {
    _id: p._id,
    fullName: p.fullName,
    mrn: p.mrn,
    status: p.status,
    admissionDate: p.admissionDate,
    department: p.department,
    ward: p.ward,
    roomBed: p.roomBed,
    primaryDoctor: p.primaryDoctor,
    cardNumber: p.cardNumber,
    relativeName: p.relativeName,
    relativePhone: p.relativePhone,
    photoUrl: p.photoUrl
  };
}

// =========================
// BEDS endpoints
// =========================

r.get('/beds', async (req, res) => {
  try {
    const mapId = String(req.query.mapId || 'F1');
    const bedIds = await getValidBedIds(mapId);
    res.json({ mapId, bedIds });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Load beds failed' });
  }
});

r.get('/by-bed/:bedId', async (req, res) => {
  try {
    const bedId = String(req.params.bedId || '').trim();
    if (!bedId) return res.status(400).json({ message: 'bedId required' });

    const aliases = getBedsAliases(bedId);
    const p = await Patient.findOne(
      { roomBed: { $in: aliases }, status: { $not: /^discharged$/i } },
      null,
      { collation: { locale: 'en', strength: 2 } }
    ).sort({ createdAt: -1 }).lean();

    res.json({ bedId, patient: toPatientSummary(p) });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Load patient by bed failed' });
  }
});

// =========================
// GET PATIENT BY RFID CARD NUMBER (for Biped Robot)
// =========================
r.get('/by-card/:cardNumber', async (req, res) => {
  try {
    const cardNumber = String(req.params.cardNumber || '').trim().toUpperCase();
    if (!cardNumber) return res.status(400).json({ message: 'cardNumber required' });

    // Tìm patient theo cardNumber (RFID UID)
    // Hỗ trợ cả format có dấu ':' và không có
    const normalizedCard = cardNumber.replace(/[:\-\s]/g, '');
    
    const p = await Patient.findOne({
      $or: [
        { cardNumber: cardNumber },
        { cardNumber: normalizedCard },
        { cardNumber: { $regex: new RegExp('^' + normalizedCard + '$', 'i') } }
      ],
      status: { $not: /^discharged$/i }
    }).lean();

    if (!p) {
      return res.status(404).json({ 
        found: false,
        message: 'Patient not found with this card' 
      });
    }

    res.json({ 
      found: true,
      patient: {
        _id: p._id,
        patientId: p._id.toString(),
        fullName: p.fullName,
        mrn: p.mrn,
        status: p.status,
        roomBed: p.roomBed,
        department: p.department,
        primaryDoctor: p.primaryDoctor,
        cardNumber: p.cardNumber,
        photoUrl: p.photoUrl
      }
    });
  } catch (e) {
    console.error('Get patient by card error:', e);
    res.status(500).json({ message: e.message || 'Load patient by card failed' });
  }
});

// =========================
// META
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
// LIST
// =========================
r.get('/', async (req, res) => {
  try {
    const { q, status, doctor, department, from, to, sort } = req.query;

    const filter = {};
    if (status) filter.status = String(status);
    if (doctor) filter.primaryDoctor = String(doctor);
    if (department) filter.department = String(department);

    const fromD = parseDateOnly(from);
    const toD = parseDateOnly(to);
    if (fromD || toD) {
      filter.admissionDate = {};
      if (fromD) filter.admissionDate.$gte = fromD;
      if (toD) filter.admissionDate.$lte = toD;
    }

    let list = await Patient.find(filter).lean();

    if (q && String(q).trim()) {
      const target = norm(q);
      list = list.filter(p => 
        norm(p.fullName || '').includes(target) ||
        norm(p.mrn || '').includes(target) ||
        norm(p.cardNumber || '').includes(target) ||
        norm(p.primaryDoctor || '').includes(target)
      );
    }

    const s = String(sort || 'bedOrder');
    if (s === 'oldestAdmission') {
      list.sort((a,b) => new Date(a.admissionDate || 0) - new Date(b.admissionDate || 0));
    } else {
      list.sort((a,b) => {
        const A = parseBedKeyAny(a.roomBed || '');
        const B = parseBedKeyAny(b.roomBed || '');
        if (A.room !== B.room) return A.room - B.room;
        return A.bed - B.bed;
      });
    }

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
// CREATE
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

    const required = ['fullName','mrn','admissionDate','status','primaryDoctor','cardNumber','relativeName','relativePhone','roomBed'];
    for (const k of required) {
      if (!doc[k]) return res.status(400).json({ message: `${k} is required` });
    }

    const canonical = normalizeBedToCanonical(doc.roomBed);
    if (!canonical) {
      return res.status(400).json({ message: `roomBed invalid format` });
    }
    doc.roomBed = canonical;

    if (!(await isValidBed(doc.roomBed))) {
      return res.status(400).json({ message: `roomBed invalid. Must be one of bed-map ids (e.g. R1M1, R1O1, ...).` });
    }

    if (String(doc.status).toLowerCase() !== 'discharged') {
      const aliases = getBedsAliases(doc.roomBed);
      const occupied = await Patient.findOne(
        { roomBed: { $in: aliases }, status: { $not: /^discharged$/i } },
        null,
        { collation: { locale: 'en', strength: 2 } }
      ).lean();
      if (occupied) {
        return res.status(409).json({ message: `Bed ${doc.roomBed} is already occupied by ${occupied.fullName}` });
      }
    }

    if (req.file) Object.assign(doc, buildPhotoFieldsFromMulterFile(req.file));

    const created = await Patient.create(doc);
    res.status(201).json(created);
  } catch (e) {
    res.status(500).json({ message: e.message || 'Create failed' });
  }
});

// =========================
// UPDATE
// =========================
r.put('/:id', upload.single('photo'), async (req, res) => {
  try {
    const id = req.params.id;
    const patient = await Patient.findById(id);
    if (!patient) return res.status(404).json({ message: 'Patient not found' });

    const body = req.body || {};

    if (req.file) {
      await safeUnlink(patient.photoPath || patient.photoUrl);
      const photo = buildPhotoFieldsFromMulterFile(req.file);
      patient.photoPath = photo.photoPath;
      patient.photoUrl = photo.photoUrl;
    }

    const assignIf = (key, val) => { if (val !== undefined) patient[key] = val; };

    assignIf('fullName', body.fullName ? String(body.fullName).trim() : undefined);
    assignIf('mrn', body.mrn ? String(body.mrn).trim() : undefined);
    assignIf('dob', body.dob ? String(body.dob) : undefined);
    assignIf('gender', body.gender ? String(body.gender) : undefined);
    assignIf('admissionDate', body.admissionDate ? String(body.admissionDate).trim() : undefined);
    assignIf('status', body.status ? String(body.status).trim() : undefined);
    assignIf('department', body.department ? String(body.department).trim() : undefined);
    assignIf('ward', body.ward ? String(body.ward).trim() : undefined);
    assignIf('primaryDoctor', body.primaryDoctor ? String(body.primaryDoctor).trim() : undefined);
    assignIf('cardNumber', body.cardNumber ? String(body.cardNumber).trim() : undefined);
    assignIf('relativeName', body.relativeName ? String(body.relativeName).trim() : undefined);
    assignIf('relativePhone', body.relativePhone ? String(body.relativePhone).trim() : undefined);
    assignIf('insurancePolicyId', body.insurancePolicyId ? String(body.insurancePolicyId).trim() : undefined);

    if (body.roomBed !== undefined) {
      const newBed = String(body.roomBed || '').trim();
      if (!newBed) return res.status(400).json({ message: 'roomBed is required' });

      const canonical = normalizeBedToCanonical(newBed);
      if (!canonical) {
        return res.status(400).json({ message: `roomBed invalid format` });
      }

      if (!(await isValidBed(canonical))) {
        return res.status(400).json({ message: `roomBed invalid. Must be one of bed-map ids (e.g. R1M1, R1O1, ...).` });
      }

      const nextStatus = body.status ? String(body.status).trim() : patient.status;
      if (String(nextStatus).toLowerCase() !== 'discharged') {
        const aliases = getBedsAliases(canonical);
        const occupied = await Patient.findOne(
          {
            _id: { $ne: patient._id },
            roomBed: { $in: aliases },
            status: { $not: /^discharged$/i }
          },
          null,
          { collation: { locale: 'en', strength: 2 } }
        ).lean();
        if (occupied) {
          return res.status(409).json({ message: `Bed ${canonical} is already occupied by ${occupied.fullName}` });
        }
      }

      patient.roomBed = canonical;
    }

    await patient.save();
    res.json(patient);
  } catch (e) {
    res.status(500).json({ message: e.message || 'Update failed' });
  }
});

// =========================
// DELETE
// =========================
r.delete('/:id', async (req, res) => {
  try {
    const id = req.params.id;
    const patient = await Patient.findById(id);
    if (!patient) return res.status(404).json({ message: 'Patient not found' });

    await safeUnlink(patient.photoPath || patient.photoUrl);
    await Patient.deleteOne({ _id: id });

    res.json({ ok: true });
  } catch (e) {
    res.status(500).json({ message: e.message || 'Delete failed' });
  }
});

// =========================
// DETAILS
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
