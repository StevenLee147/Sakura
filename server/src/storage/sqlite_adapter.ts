import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { IStorageAdapter, LeaderboardEntry, ScoreSubmission, UserRecord } from "./types.js";

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

  constructor(dataDir: string) {
    this.dataDir = path.resolve(dataDir);
    this.replaysDir = path.join(this.dataDir, "replays");
    fs.mkdirSync(this.replaysDir, { recursive: true });
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
    `);
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

  close(): void {
    this.db.close();
  }
}
