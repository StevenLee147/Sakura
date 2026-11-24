import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { validateSakuraPackage } from "../zip_validator.js";
import {
  CloudAchievementRecord,
  CloudScoreRecord,
  CloudSyncSnapshot,
  IStorageAdapter,
  LeaderboardEntry,
  MarketChartItem,
  MarketRatingEntry,
  MarketSearchQuery,
  ScoreSubmission,
  UserRecord,
} from "./types.js";

export function computeAuthoritativePP(sub: ScoreSubmission): number {
  const safeLevel = Math.max(0, sub.difficultyLevel || 0);
  const acc = Math.max(0, Math.min(1, (sub.accuracy || 0) / 100.0));
  const normScore = Math.max(0, (sub.score || 0) / 1000000.0);
  let pp = Math.pow(safeLevel, 2.5) * 10.0 * Math.pow(acc, 4.0) * Math.pow(normScore, 2.0);
  if (sub.isFullCombo) pp *= 1.05;
  if (sub.isAllPerfect) pp *= 1.10;
  if (acc >= 0.99) pp *= 1.02;
  if ((sub.synergyCount || 0) > 0) {
    pp *= 1.0 + Math.min(0.03, sub.synergyCount * 0.0015);
  }
  return Math.round(pp * 100) / 100;
}

export function validateScoreSubmission(sub: ScoreSubmission): void {
  if (sub.assisted) {
    throw new Error("Assisted plays are rejected from ranked leaderboards.");
  }
  if (!sub.chartId || !sub.chartHash || !sub.difficulty) {
    throw new Error("Missing chart_id, chart_hash, or difficulty.");
  }
  if (!Number.isFinite(sub.score) || sub.score < 0 || sub.score > 1000000) {
    throw new Error("Score exceeds 1,000,000 normalized cap or is negative.");
  }
  if (!Number.isFinite(sub.accuracy) || sub.accuracy < 0 || sub.accuracy > 100.01) {
    throw new Error("Accuracy is out of valid range [0, 100].");
  }
  const counts = [sub.perfectCount, sub.greatCount, sub.goodCount, sub.badCount, sub.missCount];
  if (counts.some((c) => !Number.isInteger(c) || c < 0)) {
    throw new Error("Invalid negative judgment counts.");
  }
  const sum = counts.reduce((a, b) => a + b, 0);
  if (sum <= 0 || (sub.totalJudgments > 0 && sum !== sub.totalJudgments)) {
    throw new Error("Judgment count sum does not match total_judgments.");
  }
  if (sub.replayData && sub.replayData.length > 0) {
    if (sub.replayData.length > 16 * 1024 * 1024) {
      throw new Error("Replay payload too large.");
    }
    let parsed: Record<string, unknown>;
    try {
      parsed = JSON.parse(sub.replayData) as Record<string, unknown>;
    } catch {
      throw new Error("Malformed .skr replay JSON.");
    }
    if (parsed.format !== "sakura-replay" || parsed.chart_hash !== sub.chartHash) {
      throw new Error("Replay chart_hash mismatch.");
    }
  }
}

export class SqliteStorageAdapter implements IStorageAdapter {
  public readonly mode: "sqlite" | "postgres" = "sqlite";
  public readonly db: DatabaseSync;
  public readonly dataDir: string;
  private readonly replaysDir: string;
  private readonly chartsDir: string;

  constructor(dataDir: string) {
    this.dataDir = path.resolve(dataDir);
    this.replaysDir = path.join(this.dataDir, "replays");
    this.chartsDir = path.join(this.dataDir, "charts");
    fs.mkdirSync(this.replaysDir, { recursive: true });
    fs.mkdirSync(this.chartsDir, { recursive: true });
    this.db = new DatabaseSync(path.join(this.dataDir, "online.db"));
    this.db.exec("PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;");
    this.initSchema();
  }

  private initSchema(): void {
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS users (
        id TEXT PRIMARY KEY,
        username TEXT NOT NULL UNIQUE COLLATE NOCASE,
        password_hash TEXT NOT NULL,
        is_guest INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS scores (
        id TEXT PRIMARY KEY,
        user_id TEXT NOT NULL,
        chart_id TEXT NOT NULL,
        chart_title TEXT NOT NULL,
        chart_hash TEXT NOT NULL,
        difficulty TEXT NOT NULL,
        difficulty_level REAL NOT NULL,
        score INTEGER NOT NULL,
        accuracy REAL NOT NULL,
        max_combo INTEGER NOT NULL,
        grade TEXT NOT NULL,
        pp REAL NOT NULL,
        perfect_count INTEGER NOT NULL,
        great_count INTEGER NOT NULL,
        good_count INTEGER NOT NULL,
        bad_count INTEGER NOT NULL,
        miss_count INTEGER NOT NULL,
        synergy_count INTEGER NOT NULL DEFAULT 0,
        is_full_combo INTEGER NOT NULL DEFAULT 0,
        is_all_perfect INTEGER NOT NULL DEFAULT 0,
        has_replay INTEGER NOT NULL DEFAULT 0,
        played_at INTEGER NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id)
      );
      CREATE INDEX IF NOT EXISTS idx_scores_lb ON scores(chart_id, difficulty, score DESC);
      CREATE TABLE IF NOT EXISTS market_charts (
        id TEXT PRIMARY KEY,
        chart_slug TEXT NOT NULL,
        uploader_id TEXT NOT NULL,
        uploader_name TEXT NOT NULL,
        title TEXT NOT NULL,
        artist TEXT NOT NULL,
        charter TEXT NOT NULL,
        description TEXT NOT NULL DEFAULT '',
        bpm REAL NOT NULL,
        min_level REAL NOT NULL,
        max_level REAL NOT NULL,
        tags_json TEXT NOT NULL DEFAULT '[]',
        difficulties_json TEXT NOT NULL DEFAULT '[]',
        package_sha256 TEXT NOT NULL,
        content_hash TEXT NOT NULL,
        package_size INTEGER NOT NULL,
        downloads INTEGER NOT NULL DEFAULT 0,
        rating_sum INTEGER NOT NULL DEFAULT 0,
        rating_count INTEGER NOT NULL DEFAULT 0,
        rating_avg REAL NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        FOREIGN KEY(uploader_id) REFERENCES users(id)
      );
      CREATE INDEX IF NOT EXISTS idx_market_updated ON market_charts(updated_at DESC);
      CREATE INDEX IF NOT EXISTS idx_market_downloads ON market_charts(downloads DESC);
      CREATE TABLE IF NOT EXISTS chart_ratings (
        chart_id TEXT NOT NULL,
        user_id TEXT NOT NULL,
        username TEXT NOT NULL,
        rating INTEGER NOT NULL,
        comment TEXT NOT NULL DEFAULT '',
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (chart_id, user_id),
        FOREIGN KEY(chart_id) REFERENCES market_charts(id),
        FOREIGN KEY(user_id) REFERENCES users(id)
      );
      CREATE TABLE IF NOT EXISTS cloud_best_scores (
        user_id TEXT NOT NULL,
        chart_hash TEXT NOT NULL,
        chart_id TEXT NOT NULL,
        chart_title TEXT NOT NULL,
        difficulty TEXT NOT NULL,
        difficulty_level REAL NOT NULL,
        score INTEGER NOT NULL,
        accuracy REAL NOT NULL,
        max_combo INTEGER NOT NULL,
        grade TEXT NOT NULL,
        perfect_count INTEGER NOT NULL,
        great_count INTEGER NOT NULL,
        good_count INTEGER NOT NULL,
        bad_count INTEGER NOT NULL,
        miss_count INTEGER NOT NULL,
        is_full_combo INTEGER NOT NULL DEFAULT 0,
        is_all_perfect INTEGER NOT NULL DEFAULT 0,
        played_at INTEGER NOT NULL,
        PRIMARY KEY (user_id, chart_id, chart_hash, difficulty),
        FOREIGN KEY(user_id) REFERENCES users(id)
      );
      CREATE TABLE IF NOT EXISTS cloud_achievements (
        user_id TEXT NOT NULL,
        achievement_id TEXT NOT NULL,
        unlocked_at INTEGER NOT NULL,
        PRIMARY KEY (user_id, achievement_id),
        FOREIGN KEY(user_id) REFERENCES users(id)
      );
      CREATE TABLE IF NOT EXISTS cloud_settings (
        user_id TEXT PRIMARY KEY,
        settings_json TEXT NOT NULL DEFAULT '{}',
        updated_at INTEGER NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id)
      );
    `);
    const cloudTbl = this.db.prepare(
      "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'cloud_best_scores'"
    ).get() as { sql?: string } | undefined;
    if (cloudTbl?.sql && !cloudTbl.sql.includes("PRIMARY KEY (user_id, chart_id, chart_hash, difficulty)")) {
      this.db.exec(`
        ALTER TABLE cloud_best_scores RENAME TO cloud_best_scores_legacy;
        CREATE TABLE cloud_best_scores (
          user_id TEXT NOT NULL,
          chart_hash TEXT NOT NULL,
          chart_id TEXT NOT NULL,
          chart_title TEXT NOT NULL,
          difficulty TEXT NOT NULL,
          difficulty_level REAL NOT NULL,
          score INTEGER NOT NULL,
          accuracy REAL NOT NULL,
          max_combo INTEGER NOT NULL,
          grade TEXT NOT NULL,
          perfect_count INTEGER NOT NULL,
          great_count INTEGER NOT NULL,
          good_count INTEGER NOT NULL,
          bad_count INTEGER NOT NULL,
          miss_count INTEGER NOT NULL,
          is_full_combo INTEGER NOT NULL DEFAULT 0,
          is_all_perfect INTEGER NOT NULL DEFAULT 0,
          played_at INTEGER NOT NULL,
          PRIMARY KEY (user_id, chart_id, chart_hash, difficulty),
          FOREIGN KEY(user_id) REFERENCES users(id)
        );
        INSERT OR IGNORE INTO cloud_best_scores SELECT * FROM cloud_best_scores_legacy;
        DROP TABLE cloud_best_scores_legacy;
      `);
    }
  }

  createGuestUser(nickname?: string): UserRecord {
    const suffix = crypto.randomBytes(3).toString("hex").toUpperCase();
    const clean = (nickname || "").trim().replace(/[^A-Za-z0-9_\u4e00-\u9fa5-]/g, "").slice(0, 16);
    const username = clean ? `${clean}_${suffix.slice(0, 4)}` : `Guest_${suffix}`;
    const id = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    this.db.prepare(
      "INSERT INTO users (id, username, password_hash, is_guest, created_at) VALUES (?, ?, '', 1, ?)"
    ).run(id, username, now);
    return { id, username, passwordHash: "", isGuest: true, createdAt: now };
  }

  registerUser(username: string, passwordHash: string): UserRecord {
    const existing = this.findUserByUsername(username);
    if (existing) throw new Error("Username already exists.");
    const id = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    this.db.prepare(
      "INSERT INTO users (id, username, password_hash, is_guest, created_at) VALUES (?, ?, ?, 0, ?)"
    ).run(id, username, passwordHash, now);
    return { id, username, passwordHash, isGuest: false, createdAt: now };
  }

  findUserByUsername(username: string): UserRecord | null {
    const row = this.db.prepare(
      "SELECT id, username, password_hash, is_guest, created_at FROM users WHERE username = ?"
    ).get(username) as Record<string, unknown> | undefined;
    if (!row) return null;
    return {
      id: String(row.id),
      username: String(row.username),
      passwordHash: String(row.password_hash),
      isGuest: Number(row.is_guest) !== 0,
      createdAt: Number(row.created_at),
    };
  }

  findUserById(id: string): UserRecord | null {
    const row = this.db.prepare(
      "SELECT id, username, password_hash, is_guest, created_at FROM users WHERE id = ?"
    ).get(id) as Record<string, unknown> | undefined;
    if (!row) return null;
    return {
      id: String(row.id),
      username: String(row.username),
      passwordHash: String(row.password_hash),
      isGuest: Number(row.is_guest) !== 0,
      createdAt: Number(row.created_at),
    };
  }

  getUserTotalPP(userId: string): number {
    const rows = this.db.prepare(
      "SELECT MAX(pp) AS best_pp FROM scores WHERE user_id = ? GROUP BY chart_id, difficulty, chart_hash ORDER BY best_pp DESC LIMIT 50"
    ).all(userId) as Array<{ best_pp: number }>;
    let total = 0;
    rows.forEach((r, idx) => {
      total += Number(r.best_pp || 0) * Math.pow(0.95, idx);
    });
    return Math.round(total * 100) / 100;
  }

  submitScore(userId: string, sub: ScoreSubmission): { scoreId: string; pp: number; rank: number; newBest: boolean } {
    validateScoreSubmission(sub);
    const prev = this.db.prepare(
      "SELECT MAX(score) AS best FROM scores WHERE user_id = ? AND chart_id = ? AND difficulty = ?"
    ).get(userId, sub.chartId, sub.difficulty) as { best: number | null } | undefined;
    const newBest = prev?.best == null || sub.score > Number(prev.best);

    const pp = computeAuthoritativePP(sub);
    const scoreId = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    const hasReplay = Boolean(sub.replayData && sub.replayData.length > 0);

    if (hasReplay && sub.replayData) {
      fs.writeFileSync(path.join(this.replaysDir, `${scoreId}.skr`), sub.replayData, "utf-8");
    }

    this.db.prepare(`
      INSERT INTO scores (
        id, user_id, chart_id, chart_title, chart_hash, difficulty, difficulty_level,
        score, accuracy, max_combo, grade, pp,
        perfect_count, great_count, good_count, bad_count, miss_count,
        synergy_count, is_full_combo, is_all_perfect, has_replay, played_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `).run(
      scoreId, userId, sub.chartId, sub.chartTitle || sub.chartId, sub.chartHash,
      sub.difficulty, sub.difficultyLevel, sub.score, sub.accuracy, sub.maxCombo,
      sub.grade || "D", pp, sub.perfectCount, sub.greatCount, sub.goodCount,
      sub.badCount, sub.missCount, sub.synergyCount || 0,
      sub.isFullCombo ? 1 : 0, sub.isAllPerfect ? 1 : 0, hasReplay ? 1 : 0, now
    );

    const lb = this.getLeaderboard(sub.chartId, sub.difficulty, 1000, userId);
    return { scoreId, pp, rank: lb.myRank ?? 1, newBest };
  }

  getLeaderboard(chartId: string, difficulty: string, limit: number, currentUserId?: string): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  } {
    const capped = Math.max(1, Math.min(100, limit || 50));
    const rows = this.db.prepare(`
      SELECT s.id, s.user_id, u.username, s.score, s.accuracy, s.max_combo, s.grade, s.pp,
             s.is_full_combo, s.is_all_perfect, s.has_replay, s.played_at
      FROM scores s
      JOIN users u ON u.id = s.user_id
      WHERE s.chart_id = ? AND s.difficulty = ?
      ORDER BY s.score DESC, s.accuracy DESC, s.played_at ASC
    `).all(chartId, difficulty) as Array<Record<string, unknown>>;

    const seenUsers = new Set<string>();
    const deduped: LeaderboardEntry[] = [];
    let myRank: number | null = null;

    for (const r of rows) {
      const uid = String(r.user_id);
      if (seenUsers.has(uid)) continue;
      seenUsers.add(uid);
      const rank = deduped.length + 1;
      if (currentUserId && uid === currentUserId) {
        myRank = rank;
      }
      if (deduped.length < capped) {
        deduped.push({
          rank,
          scoreId: String(r.id),
          userId: uid,
          username: String(r.username),
          score: Number(r.score),
          accuracy: Number(r.accuracy),
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          pp: Number(r.pp),
          isFullCombo: Number(r.is_full_combo) !== 0,
          isAllPerfect: Number(r.is_all_perfect) !== 0,
          hasReplay: Number(r.has_replay) !== 0,
          playedAt: Number(r.played_at),
        });
      }
    }
    return { entries: deduped, myRank };
  }

  getReplayData(scoreId: string): string | null {
    if (!/^[0-9a-fA-F-]{36}$/.test(scoreId)) return null;
    const p = path.join(this.replaysDir, `${scoreId}.skr`);
    if (!fs.existsSync(p)) return null;
    return fs.readFileSync(p, "utf-8");
  }

  private rowToMarketChart(row: Record<string, unknown>, myRating?: number | null): MarketChartItem {
    let tags: string[] = [];
    let diffs: MarketChartItem["difficulties"] = [];
    try { tags = JSON.parse(String(row.tags_json || "[]")); } catch { tags = []; }
    try { diffs = JSON.parse(String(row.difficulties_json || "[]")); } catch { diffs = []; }
    return {
      id: String(row.id),
      chartSlug: String(row.chart_slug),
      uploaderId: String(row.uploader_id),
      uploaderName: String(row.uploader_name),
      title: String(row.title),
      artist: String(row.artist),
      charter: String(row.charter),
      description: String(row.description || ""),
      bpm: Number(row.bpm),
      minLevel: Number(row.min_level),
      maxLevel: Number(row.max_level),
      tags,
      difficulties: diffs,
      packageSha256: String(row.package_sha256),
      contentHash: String(row.content_hash),
      packageSize: Number(row.package_size),
      downloads: Number(row.downloads),
      ratingAvg: Math.round(Number(row.rating_avg || 0) * 100) / 100,
      ratingCount: Number(row.rating_count || 0),
      myRating: myRating === undefined ? null : myRating,
      createdAt: Number(row.created_at),
      updatedAt: Number(row.updated_at),
    };
  }

  publishChart(userId: string, username: string, pkgBuffer: Buffer, description = ""): MarketChartItem {
    const meta = validateSakuraPackage(pkgBuffer, description);
    const now = Math.floor(Date.now() / 1000);

    const existingSameAuthor = this.db.prepare(
      "SELECT id FROM market_charts WHERE chart_slug = ? AND uploader_id = ?"
    ).get(meta.chartSlug, userId) as { id: string } | undefined;

    let marketId = existingSameAuthor?.id ?? "";
    if (!marketId) {
      const slugTaken = this.db.prepare("SELECT id FROM market_charts WHERE id = ?").get(meta.chartSlug);
      marketId = slugTaken ? `${meta.chartSlug}_${userId.slice(0, 6)}` : meta.chartSlug;
    }

    fs.writeFileSync(path.join(this.chartsDir, `${marketId}.sakura`), pkgBuffer);

    if (existingSameAuthor) {
      this.db.prepare(`
        UPDATE market_charts SET
          uploader_name = ?, title = ?, artist = ?, charter = ?, description = ?,
          bpm = ?, min_level = ?, max_level = ?, tags_json = ?, difficulties_json = ?,
          package_sha256 = ?, content_hash = ?, package_size = ?, updated_at = ?
        WHERE id = ?
      `).run(
        username, meta.title, meta.artist, meta.charter, meta.description,
        meta.bpm, meta.minLevel, meta.maxLevel, JSON.stringify(meta.tags),
        JSON.stringify(meta.difficulties), meta.packageSha256, meta.contentHash,
        meta.packageSize, now, marketId
      );
    } else {
      this.db.prepare(`
        INSERT INTO market_charts (
          id, chart_slug, uploader_id, uploader_name, title, artist, charter, description,
          bpm, min_level, max_level, tags_json, difficulties_json, package_sha256,
          content_hash, package_size, downloads, rating_sum, rating_count, rating_avg, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0, 0, ?, ?)
      `).run(
        marketId, meta.chartSlug, userId, username, meta.title, meta.artist, meta.charter,
        meta.description, meta.bpm, meta.minLevel, meta.maxLevel, JSON.stringify(meta.tags),
        JSON.stringify(meta.difficulties), meta.packageSha256, meta.contentHash,
        meta.packageSize, now, now
      );
    }

    return this.getChartById(marketId, userId)!.chart;
  }

  listCharts(query: MarketSearchQuery): { total: number; page: number; pageSize: number; charts: MarketChartItem[] } {
    const page = Math.max(1, Math.trunc(query.page || 1));
    const pageSize = Math.max(1, Math.min(50, Math.trunc(query.pageSize || 20)));
    const q = (query.q || "").trim().toLowerCase();
    const tag = (query.tag || "").trim().toLowerCase();
    const minLevel = Number.isFinite(query.minLevel) ? Number(query.minLevel) : 0;
    const maxLevel = Number.isFinite(query.maxLevel) ? Number(query.maxLevel) : 100;

    let orderClause = "updated_at DESC, downloads DESC";
    if (query.sort === "popular") orderClause = "downloads DESC, rating_avg DESC, updated_at DESC";
    else if (query.sort === "rating") orderClause = "rating_avg DESC, rating_count DESC, downloads DESC";

    const allRows = this.db.prepare(
      `SELECT * FROM market_charts WHERE max_level >= ? AND min_level <= ? ORDER BY ${orderClause}`
    ).all(minLevel, maxLevel) as Array<Record<string, unknown>>;

    const filtered = allRows.filter((r) => {
      if (q) {
        const hay = `${r.title} ${r.artist} ${r.charter} ${r.uploader_name} ${r.tags_json}`.toLowerCase();
        if (!hay.includes(q)) return false;
      }
      if (tag) {
        const tagsLower = String(r.tags_json || "").toLowerCase();
        if (!tagsLower.includes(tag)) return false;
      }
      return true;
    });

    const userRatings = new Map<string, number>();
    if (query.currentUserId) {
      const rRows = this.db.prepare("SELECT chart_id, rating FROM chart_ratings WHERE user_id = ?").all(query.currentUserId) as Array<{ chart_id: string; rating: number }>;
      for (const rr of rRows) userRatings.set(String(rr.chart_id), Number(rr.rating));
    }

    const slice = filtered.slice((page - 1) * pageSize, page * pageSize);
    return {
      total: filtered.length,
      page,
      pageSize,
      charts: slice.map((r) => this.rowToMarketChart(r, userRatings.get(String(r.id)) ?? null)),
    };
  }

  getChartById(chartId: string, currentUserId?: string): { chart: MarketChartItem; ratings: MarketRatingEntry[] } | null {
    const row = this.db.prepare("SELECT * FROM market_charts WHERE id = ?").get(chartId) as Record<string, unknown> | undefined;
    if (!row) return null;
    let myRating: number | null = null;
    if (currentUserId) {
      const mr = this.db.prepare("SELECT rating FROM chart_ratings WHERE chart_id = ? AND user_id = ?").get(chartId, currentUserId) as { rating: number } | undefined;
      if (mr) myRating = Number(mr.rating);
    }
    const ratingRows = this.db.prepare(
      "SELECT user_id, username, rating, comment, updated_at FROM chart_ratings WHERE chart_id = ? ORDER BY updated_at DESC LIMIT 20"
    ).all(chartId) as Array<Record<string, unknown>>;
    const ratings: MarketRatingEntry[] = ratingRows.map((rr) => ({
      userId: String(rr.user_id),
      username: String(rr.username),
      rating: Number(rr.rating),
      comment: String(rr.comment || ""),
      updatedAt: Number(rr.updated_at),
    }));
    return { chart: this.rowToMarketChart(row, myRating), ratings };
  }

  downloadChartPackage(chartId: string): { buffer: Buffer; chart: MarketChartItem } | null {
    if (!/^[A-Za-z0-9_\u4e00-\u9fa5-]{1,64}$/.test(chartId)) return null;
    const detail = this.getChartById(chartId);
    if (!detail) return null;
    const filePath = path.join(this.chartsDir, `${chartId}.sakura`);
    if (!fs.existsSync(filePath)) return null;
    this.db.prepare("UPDATE market_charts SET downloads = downloads + 1 WHERE id = ?").run(chartId);
    const updated = this.getChartById(chartId)!;
    return { buffer: fs.readFileSync(filePath), chart: updated.chart };
  }

  rateChart(chartId: string, userId: string, username: string, rating: number, comment: string): { ratingAvg: number; ratingCount: number; myRating: number } {
    const rInt = Math.trunc(rating);
    if (!Number.isInteger(rInt) || rInt < 1 || rInt > 5) {
      throw new Error("Rating must be an integer between 1 and 5.");
    }
    const exists = this.db.prepare("SELECT id FROM market_charts WHERE id = ?").get(chartId);
    if (!exists) throw new Error("Chart not found.");
    const now = Math.floor(Date.now() / 1000);
    const cleanComment = (comment || "").trim().slice(0, 200);
    this.db.prepare(`
      INSERT INTO chart_ratings (chart_id, user_id, username, rating, comment, updated_at)
      VALUES (?, ?, ?, ?, ?, ?)
      ON CONFLICT(chart_id, user_id) DO UPDATE SET
        username = excluded.username,
        rating = excluded.rating,
        comment = excluded.comment,
        updated_at = excluded.updated_at
    `).run(chartId, userId, username, rInt, cleanComment, now);

    const agg = this.db.prepare(
      "SELECT COALESCE(SUM(rating), 0) AS s, COUNT(*) AS c FROM chart_ratings WHERE chart_id = ?"
    ).get(chartId) as { s: number; c: number };
    const sum = Number(agg.s || 0);
    const count = Number(agg.c || 0);
    const avg = count > 0 ? Math.round((sum / count) * 100) / 100 : 0;
    this.db.prepare("UPDATE market_charts SET rating_sum = ?, rating_count = ?, rating_avg = ? WHERE id = ?").run(sum, count, avg, chartId);
    return { ratingAvg: avg, ratingCount: count, myRating: rInt };
  }

  pushCloudSync(
    userId: string,
    payload: {
      scores: CloudScoreRecord[];
      achievements: CloudAchievementRecord[];
      settings?: Record<string, unknown>;
      settingsUpdatedAt?: number;
    }
  ): { scoresMerged: number; achievementsMerged: number; snapshot: CloudSyncSnapshot } {
    const now = Math.floor(Date.now() / 1000);
    let scoresMerged = 0;
    let achievementsMerged = 0;

    const selectBest = this.db.prepare(
      "SELECT score, accuracy FROM cloud_best_scores WHERE user_id = ? AND chart_id = ? AND chart_hash = ? AND difficulty = ?"
    );
    const upsertBest = this.db.prepare(`
      INSERT INTO cloud_best_scores (
        user_id, chart_hash, chart_id, chart_title, difficulty, difficulty_level,
        score, accuracy, max_combo, grade,
        perfect_count, great_count, good_count, bad_count, miss_count,
        is_full_combo, is_all_perfect, played_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT(user_id, chart_id, chart_hash, difficulty) DO UPDATE SET
        chart_title = excluded.chart_title,
        difficulty_level = excluded.difficulty_level,
        score = excluded.score,
        accuracy = excluded.accuracy,
        max_combo = excluded.max_combo,
        grade = excluded.grade,
        perfect_count = excluded.perfect_count,
        great_count = excluded.great_count,
        good_count = excluded.good_count,
        bad_count = excluded.bad_count,
        miss_count = excluded.miss_count,
        is_full_combo = excluded.is_full_combo,
        is_all_perfect = excluded.is_all_perfect,
        played_at = excluded.played_at
    `);

    for (const s of payload.scores || []) {
      if (!s || !s.chartHash || !s.difficulty || !Number.isFinite(s.score) || s.score < 0 || s.score > 1000000) {
        continue;
      }
      const cid = s.chartId || "unknown";
      const prev = selectBest.get(userId, cid, s.chartHash, s.difficulty) as { score: number; accuracy: number } | undefined;
      const isBetter = !prev || s.score > Number(prev.score) || (s.score === Number(prev.score) && s.accuracy > Number(prev.accuracy));
      if (isBetter) {
        upsertBest.run(
          userId,
          s.chartHash,
          cid,
          s.chartTitle || cid || "Unknown",
          s.difficulty,
          Number(s.difficultyLevel || 1),
          Math.trunc(s.score),
          Number(s.accuracy || 0),
          Math.trunc(s.maxCombo || 0),
          s.grade || "D",
          Math.trunc(s.perfectCount || 0),
          Math.trunc(s.greatCount || 0),
          Math.trunc(s.goodCount || 0),
          Math.trunc(s.badCount || 0),
          Math.trunc(s.missCount || 0),
          s.isFullCombo ? 1 : 0,
          s.isAllPerfect ? 1 : 0,
          Math.trunc(s.playedAt || now)
        );
        scoresMerged++;
      }
    }

    const selectAch = this.db.prepare(
      "SELECT unlocked_at FROM cloud_achievements WHERE user_id = ? AND achievement_id = ?"
    );
    const upsertAch = this.db.prepare(`
      INSERT INTO cloud_achievements (user_id, achievement_id, unlocked_at)
      VALUES (?, ?, ?)
      ON CONFLICT(user_id, achievement_id) DO UPDATE SET
        unlocked_at = MIN(cloud_achievements.unlocked_at, excluded.unlocked_at)
    `);

    for (const a of payload.achievements || []) {
      if (!a || typeof a.id !== "string" || !a.id.trim()) continue;
      const ts = a.unlockedAt && a.unlockedAt > 0 ? Math.trunc(a.unlockedAt) : now;
      const prev = selectAch.get(userId, a.id.trim()) as { unlocked_at: number } | undefined;
      if (!prev || ts < Number(prev.unlocked_at)) {
        achievementsMerged++;
      }
      upsertAch.run(userId, a.id.trim(), ts);
    }

    if (payload.settings && typeof payload.settings === "object" && Object.keys(payload.settings).length > 0) {
      const incomingTs = payload.settingsUpdatedAt && payload.settingsUpdatedAt > 0 ? Math.trunc(payload.settingsUpdatedAt) : now;
      const prevSet = this.db.prepare("SELECT settings_json, updated_at FROM cloud_settings WHERE user_id = ?").get(userId) as { settings_json: string; updated_at: number } | undefined;
      let mergedObj: Record<string, unknown> = {};
      if (prevSet) {
        try { mergedObj = JSON.parse(String(prevSet.settings_json || "{}")); } catch { mergedObj = {}; }
      }
      if (!prevSet || incomingTs >= Number(prevSet.updated_at)) {
        mergedObj = { ...mergedObj, ...payload.settings };
        this.db.prepare(`
          INSERT INTO cloud_settings (user_id, settings_json, updated_at)
          VALUES (?, ?, ?)
          ON CONFLICT(user_id) DO UPDATE SET settings_json = excluded.settings_json, updated_at = excluded.updated_at
        `).run(userId, JSON.stringify(mergedObj), Math.max(now, incomingTs));
      }
    }

    return {
      scoresMerged,
      achievementsMerged,
      snapshot: this.pullCloudSync(userId),
    };
  }

  pullCloudSync(userId: string): CloudSyncSnapshot {
    const now = Math.floor(Date.now() / 1000);
    const bestMap = new Map<string, CloudScoreRecord>();

    const rankedRows = this.db.prepare(
      "SELECT * FROM scores WHERE user_id = ? ORDER BY score DESC, accuracy DESC"
    ).all(userId) as Array<Record<string, unknown>>;
    for (const r of rankedRows) {
      const key = `${r.chart_id}::${r.chart_hash}::${r.difficulty}`;
      if (!bestMap.has(key)) {
        bestMap.set(key, {
          chartId: String(r.chart_id),
          chartTitle: String(r.chart_title),
          chartHash: String(r.chart_hash),
          difficulty: String(r.difficulty),
          difficultyLevel: Number(r.difficulty_level),
          score: Number(r.score),
          accuracy: Number(r.accuracy),
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          perfectCount: Number(r.perfect_count),
          greatCount: Number(r.great_count),
          goodCount: Number(r.good_count),
          badCount: Number(r.bad_count),
          missCount: Number(r.miss_count),
          isFullCombo: Number(r.is_full_combo) !== 0,
          isAllPerfect: Number(r.is_all_perfect) !== 0,
          playedAt: Number(r.played_at),
        });
      }
    }

    const cloudRows = this.db.prepare(
      "SELECT * FROM cloud_best_scores WHERE user_id = ?"
    ).all(userId) as Array<Record<string, unknown>>;
    for (const r of cloudRows) {
      const key = `${r.chart_id}::${r.chart_hash}::${r.difficulty}`;
      const prev = bestMap.get(key);
      const sc = Number(r.score);
      const acc = Number(r.accuracy);
      if (!prev || sc > prev.score || (sc === prev.score && acc > prev.accuracy)) {
        bestMap.set(key, {
          chartId: String(r.chart_id),
          chartTitle: String(r.chart_title),
          chartHash: String(r.chart_hash),
          difficulty: String(r.difficulty),
          difficultyLevel: Number(r.difficulty_level),
          score: sc,
          accuracy: acc,
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          perfectCount: Number(r.perfect_count),
          greatCount: Number(r.great_count),
          goodCount: Number(r.good_count),
          badCount: Number(r.bad_count),
          missCount: Number(r.miss_count),
          isFullCombo: Number(r.is_full_combo) !== 0,
          isAllPerfect: Number(r.is_all_perfect) !== 0,
          playedAt: Number(r.played_at),
        });
      }
    }

    const achRows = this.db.prepare(
      "SELECT achievement_id, unlocked_at FROM cloud_achievements WHERE user_id = ? ORDER BY unlocked_at ASC"
    ).all(userId) as Array<{ achievement_id: string; unlocked_at: number }>;

    const setRow = this.db.prepare(
      "SELECT settings_json, updated_at FROM cloud_settings WHERE user_id = ?"
    ).get(userId) as { settings_json: string; updated_at: number } | undefined;
    let settings: Record<string, unknown> = {};
    let settingsUpdatedAt = 0;
    if (setRow) {
      try { settings = JSON.parse(String(setRow.settings_json || "{}")); } catch { settings = {}; }
      settingsUpdatedAt = Number(setRow.updated_at || 0);
    }

    return {
      syncedAt: now,
      scores: Array.from(bestMap.values()),
      achievements: achRows.map((a) => ({ id: String(a.achievement_id), unlockedAt: Number(a.unlocked_at) })),
      settings,
      settingsUpdatedAt,
    };
  }

  close(): void {
    this.db.close();
  }
}
