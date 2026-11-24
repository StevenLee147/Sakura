import crypto from "node:crypto";
import zlib from "node:zlib";

export interface DifficultySummary {
  name: string;
  level: number;
  chartFile: string;
  noteCount: number;
}

export interface ValidatedSakuraPackage {
  chartSlug: string;
  title: string;
  artist: string;
  charter: string;
  description: string;
  bpm: number;
  minLevel: number;
  maxLevel: number;
  tags: string[];
  difficulties: DifficultySummary[];
  packageSha256: string;
  contentHash: string;
  packageSize: number;
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) !== 0 ? (0xedb88320 ^ (c >>> 1)) >>> 0 : c >>> 1;
    }
    table[i] = c >>> 0;
  }
  return table;
})();

export function computeCrc32(buf: Buffer): number {
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    crc = (CRC_TABLE[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8)) >>> 0;
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function isSafeArchivePath(rawName: string): boolean {
  if (!rawName || rawName.length > 512) return false;
  if (rawName.includes("\0") || rawName.includes(":") || rawName.startsWith("/") || rawName.startsWith("\\")) {
    return false;
  }
  const normalized = rawName.replace(/\\/g, "/");
  for (const part of normalized.split("/")) {
    if (part === ".." || part === ".") return false;
  }
  return true;
}

export function extractZipEntries(zipBuf: Buffer, maxTotalBytes = 64 * 1024 * 1024): Map<string, Buffer> {
  if (zipBuf.length < 22) {
    throw new Error("Invalid .sakura archive: file too small.");
  }
  let eocdOffset = -1;
  const minSearch = Math.max(0, zipBuf.length - 65557);
  for (let i = zipBuf.length - 22; i >= minSearch; i--) {
    if (zipBuf.readUInt32LE(i) === 0x06054b50) {
      eocdOffset = i;
      break;
    }
  }
  if (eocdOffset < 0) {
    throw new Error("Invalid .sakura archive: missing PKZIP End-of-Central-Directory.");
  }

  const entryCount = zipBuf.readUInt16LE(eocdOffset + 10);
  const cdSize = zipBuf.readUInt32LE(eocdOffset + 12);
  const cdOffset = zipBuf.readUInt32LE(eocdOffset + 16);
  if (entryCount === 0 || entryCount > 512) {
    throw new Error("Invalid .sakura archive: entry count out of allowed range (1..512).");
  }
  if (cdOffset + cdSize > eocdOffset) {
    throw new Error("Invalid .sakura archive: corrupted central directory bounds.");
  }

  const files = new Map<string, Buffer>();
  let ptr = cdOffset;
  let totalUncompressed = 0;

  for (let idx = 0; idx < entryCount; idx++) {
    if (ptr + 46 > eocdOffset || zipBuf.readUInt32LE(ptr) !== 0x02014b50) {
      throw new Error("Invalid .sakura archive: bad central directory header.");
    }
    const method = zipBuf.readUInt16LE(ptr + 10);
    const expectedCrc = zipBuf.readUInt32LE(ptr + 16);
    const compSize = zipBuf.readUInt32LE(ptr + 20);
    const uncompSize = zipBuf.readUInt32LE(ptr + 24);
    const nameLen = zipBuf.readUInt16LE(ptr + 28);
    const extraLen = zipBuf.readUInt16LE(ptr + 30);
    const commentLen = zipBuf.readUInt16LE(ptr + 32);
    const externalAttr = zipBuf.readUInt32LE(ptr + 38);
    const localHeaderOffset = zipBuf.readUInt32LE(ptr + 42);

    const unixMode = (externalAttr >>> 16) & 0xf000;
    if (unixMode === 0xa000) {
      throw new Error("Archive symlinks are forbidden.");
    }

    const rawName = zipBuf.subarray(ptr + 46, ptr + 46 + nameLen).toString("utf8");
    ptr += 46 + nameLen + extraLen + commentLen;

    const normName = rawName.replace(/\\/g, "/");
    if (normName.endsWith("/")) continue;
    if (!isSafeArchivePath(normName)) {
      throw new Error(`Unsafe path in .sakura archive: ${rawName}`);
    }

    totalUncompressed += uncompSize;
    if (totalUncompressed > maxTotalBytes) {
      throw new Error("Uncompressed .sakura package exceeds maximum allowed size.");
    }

    if (localHeaderOffset + 30 > cdOffset || zipBuf.readUInt32LE(localHeaderOffset) !== 0x04034b50) {
      throw new Error("Corrupted local file header in .sakura archive.");
    }
    const localNameLen = zipBuf.readUInt16LE(localHeaderOffset + 26);
    const localExtraLen = zipBuf.readUInt16LE(localHeaderOffset + 28);
    const dataStart = localHeaderOffset + 30 + localNameLen + localExtraLen;
    const dataEnd = dataStart + compSize;
    if (dataEnd > cdOffset) {
      throw new Error("Truncated entry payload in .sakura archive.");
    }

    const rawSlice = zipBuf.subarray(dataStart, dataEnd);
    let content: Buffer;
    if (method === 0) {
      content = Buffer.from(rawSlice);
    } else if (method === 8) {
      content = zlib.inflateRawSync(rawSlice, { maxOutputLength: maxTotalBytes });
    } else {
      throw new Error(`Unsupported ZIP compression method: ${method}`);
    }

    if (content.length !== uncompSize) {
      throw new Error(`Size mismatch for ${normName}`);
    }
    if (computeCrc32(content) !== expectedCrc) {
      throw new Error(`CRC32 mismatch for ${normName}`);
    }
    files.set(normName, content);
  }

  return files;
}

export function createStoreZipArchive(entries: Record<string, Buffer | string>): Buffer {
  const names = Object.keys(entries).sort();
  const localParts: Buffer[] = [];
  const centralParts: Buffer[] = [];
  let offset = 0;

  for (const rawName of names) {
    const nameBuf = Buffer.from(rawName, "utf8");
    const dataBuf = Buffer.isBuffer(entries[rawName]) ? (entries[rawName] as Buffer) : Buffer.from(entries[rawName] as string, "utf8");
    const crc = computeCrc32(dataBuf);

    const lh = Buffer.alloc(30);
    lh.writeUInt32LE(0x04034b50, 0);
    lh.writeUInt16LE(20, 4);
    lh.writeUInt16LE(0x0800, 6);
    lh.writeUInt16LE(0, 8);
    lh.writeUInt32LE(crc, 14);
    lh.writeUInt32LE(dataBuf.length, 18);
    lh.writeUInt32LE(dataBuf.length, 22);
    lh.writeUInt16LE(nameBuf.length, 26);
    localParts.push(lh, nameBuf, dataBuf);

    const ch = Buffer.alloc(46);
    ch.writeUInt32LE(0x02014b50, 0);
    ch.writeUInt16LE(20, 4);
    ch.writeUInt16LE(20, 6);
    ch.writeUInt16LE(0x0800, 8);
    ch.writeUInt16LE(0, 10);
    ch.writeUInt32LE(crc, 16);
    ch.writeUInt32LE(dataBuf.length, 20);
    ch.writeUInt32LE(dataBuf.length, 24);
    ch.writeUInt16LE(nameBuf.length, 28);
    ch.writeUInt32LE(offset, 42);
    centralParts.push(ch, nameBuf);

    offset += 30 + nameBuf.length + dataBuf.length;
  }

  const cdBuf = Buffer.concat(centralParts);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(names.length, 8);
  eocd.writeUInt16LE(names.length, 10);
  eocd.writeUInt32LE(cdBuf.length, 12);
  eocd.writeUInt32LE(offset, 16);
  return Buffer.concat([...localParts, cdBuf, eocd]);
}

export function validateSakuraPackage(zipBuf: Buffer, customDescription = ""): ValidatedSakuraPackage {
  if (zipBuf.length > 64 * 1024 * 1024) {
    throw new Error("Package exceeds 64 MB upload limit.");
  }
  const entries = extractZipEntries(zipBuf);
  let prefix = "";
  if (!entries.has("info.json")) {
    for (const key of entries.keys()) {
      if (key.endsWith("/info.json") && key.split("/").length === 2) {
        prefix = key.slice(0, key.length - "info.json".length);
        break;
      }
    }
  }
  const infoRaw = entries.get(`${prefix}info.json`);
  if (!infoRaw) {
    throw new Error("Missing info.json in .sakura package.");
  }

  let info: Record<string, unknown>;
  try {
    info = JSON.parse(infoRaw.toString("utf8")) as Record<string, unknown>;
  } catch {
    throw new Error("Invalid JSON in info.json.");
  }

  const rawId = typeof info.id === "string" ? info.id.trim() : "";
  const title = typeof info.title === "string" ? info.title.trim() : "";
  const artist = typeof info.artist === "string" ? info.artist.trim() : "Unknown";
  const charter = typeof info.charter === "string" ? info.charter.trim() : "Unknown";
  const description = (customDescription || (typeof info.description === "string" ? info.description : typeof info.source === "string" ? info.source : "")).trim().slice(0, 500);
  const bpm = Number(info.bpm ?? 120);

  if (!title) throw new Error("info.json requires a non-empty title.");
  if (!Number.isFinite(bpm) || bpm <= 0 || bpm > 1000) {
    throw new Error("info.json bpm must be in (0, 1000].");
  }

  const rawSlug = (rawId || title).toLowerCase().replace(/[^a-z0-9_\u4e00-\u9fa5-]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 48) || "chart";
  const tags: string[] = [];
  if (Array.isArray(info.tags)) {
    for (const t of info.tags) {
      if (typeof t === "string" && t.trim().length > 0 && tags.length < 12) {
        tags.push(t.trim().slice(0, 24));
      }
    }
  }

  const rawDiffs = info.difficulties;
  if (!Array.isArray(rawDiffs) || rawDiffs.length === 0 || rawDiffs.length > 16) {
    throw new Error("info.json must define 1..16 difficulties.");
  }

  const contentHasher = crypto.createHash("sha256");
  contentHasher.update(infoRaw);

  const difficulties: DifficultySummary[] = [];
  let minLevel = Infinity;
  let maxLevel = -Infinity;
  let totalNotesAcrossPackage = 0;

  for (const d of rawDiffs) {
    if (!d || typeof d !== "object") throw new Error("Malformed difficulty entry in info.json.");
    const dobj = d as Record<string, unknown>;
    const diffName = typeof dobj.name === "string" ? dobj.name.trim() : "Normal";
    const level = Number(dobj.level ?? 1);
    const chartFile = typeof dobj.chart_file === "string" ? dobj.chart_file : typeof dobj.chartFile === "string" ? dobj.chartFile : "";
    if (!diffName || !chartFile || !isSafeArchivePath(chartFile)) {
      throw new Error(`Invalid chart_file in difficulty '${diffName}'.`);
    }
    if (!Number.isFinite(level) || level < 0 || level > 100) {
      throw new Error(`Difficulty level out of range [0, 100] in '${diffName}'.`);
    }
    const chartRaw = entries.get(`${prefix}${chartFile.replace(/\\/g, "/")}`);
    if (!chartRaw) {
      throw new Error(`Difficulty file '${chartFile}' missing from .sakura archive.`);
    }
    contentHasher.update(chartRaw);

    let chartJson: Record<string, unknown>;
    try {
      chartJson = JSON.parse(chartRaw.toString("utf8")) as Record<string, unknown>;
    } catch {
      throw new Error(`Invalid JSON in difficulty '${chartFile}'.`);
    }

    const kbNotes = Array.isArray(chartJson.keyboard_notes) ? chartJson.keyboard_notes : [];
    const msNotes = Array.isArray(chartJson.mouse_notes) ? chartJson.mouse_notes : [];
    for (const kn of kbNotes) {
      const n = kn as Record<string, unknown>;
      const time = Number(n.time ?? -1);
      const lane = Number(n.lane ?? -1);
      const dur = Number(n.duration ?? 0);
      if (!Number.isFinite(time) || time < 0 || !Number.isInteger(lane) || lane < 0 || lane > 4 || !Number.isFinite(dur) || dur < 0) {
        throw new Error(`Invalid keyboard note in '${chartFile}'.`);
      }
    }
    for (const mn of msNotes) {
      const n = mn as Record<string, unknown>;
      const time = Number(n.time ?? -1);
      const x = Number(n.x ?? -1);
      const y = Number(n.y ?? -1);
      const dur = Number(n.slider_duration ?? 0);
      if (!Number.isFinite(time) || time < 0 || !Number.isFinite(x) || x < 0 || x > 1 || !Number.isFinite(y) || y < 0 || y > 1 || !Number.isFinite(dur) || dur < 0) {
        throw new Error(`Invalid mouse note in '${chartFile}'.`);
      }
    }

    const noteCount = kbNotes.length + msNotes.length;
    totalNotesAcrossPackage += noteCount;
    minLevel = Math.min(minLevel, level);
    maxLevel = Math.max(maxLevel, level);
    difficulties.push({ name: diffName, level, chartFile, noteCount });
  }

  if (totalNotesAcrossPackage <= 0) {
    throw new Error("Published .sakura package must contain at least 1 playable note.");
  }

  return {
    chartSlug: rawSlug,
    title,
    artist,
    charter,
    description,
    bpm,
    minLevel,
    maxLevel,
    tags,
    difficulties,
    packageSha256: crypto.createHash("sha256").update(zipBuf).digest("hex"),
    contentHash: contentHasher.digest("hex"),
    packageSize: zipBuf.length,
  };
}
