import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { validateSakuraPackage } from "../zip_validator.js";
import {
  ArenaChatMessage,
  ArenaChartSelection,
  ArenaCreateRoomInput,
  ArenaPlayerRecord,
  ArenaProgressInput,
  ArenaRoomRecord,
  ArenaRoomStatus,
  AuthSessionRecord,
  CloudAchievementRecord,
  CloudCourseRecord,
  CloudScoreRecord,
  CloudSyncSnapshot,
  DailyChallengeSpec,
  IStorageAdapter,
  LeaderboardEntry,
  MarketChartItem,
  MarketRatingEntry,
  MarketSearchQuery,
  RatingLadderEntry,
  RivalSummaryItem,
  ScoreSubmission,
  SeasonCurrentPayload,
  SeasonLeaderboardEntry,
  SeasonMyStanding,
  SeasonStageSpec,
  UserProfileData,
  UserProfileScoreItem,
  UserRecord,
} from "./types.js";

const DAILY_CHALLENGE_POOL: ReadonlyArray<Omit<DailyChallengeSpec, "date">> = [
  { chartId: "sakura_awakening", chartTitle: "樱光初醒", difficulty: "Normal", difficultyLevel: 4.5, goalText: "春光初醒 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "sakura_awakening", chartTitle: "樱光初醒", difficulty: "Hard", difficultyLevel: 7.0, goalText: "樱枝觉醒 · 达成 955,000 分", targetScore: 955000 },
  { chartId: "starry_sky", chartTitle: "繁星", difficulty: "Hard", difficultyLevel: 6.5, goalText: "星夜漫游 · 达成 955,000 分", targetScore: 955000 },
  { chartId: "distant_star", chartTitle: "遥远的星", difficulty: "Normal", difficultyLevel: 4.5, goalText: "遥望星河 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "sakura_breath", chartTitle: "樱落呼吸", difficulty: "Hard", difficultyLevel: 7.5, goalText: "呼吸律动 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "distant_star", chartTitle: "遥远的星", difficulty: "Hard", difficultyLevel: 7.5, goalText: "星轨同押 · 达成 955,000 分", targetScore: 955000 },
  { chartId: "unfallen_light", chartTitle: "樱光未落", difficulty: "Hard", difficultyLevel: 8.0, goalText: "疾风鼓点 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "flowing_petals", chartTitle: "樱落流光", difficulty: "Normal", difficultyLevel: 5.0, goalText: "流光滑条 · 达成 945,000 分", targetScore: 945000 },
  { chartId: "sidechain_dream", chartTitle: "樱落侧链梦", difficulty: "Hard", difficultyLevel: 8.5, goalText: "律动侧链 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "flowing_petals", chartTitle: "樱落流光", difficulty: "Hard", difficultyLevel: 8.5, goalText: "流光花影 · 达成 950,000 分", targetScore: 950000 },
  { chartId: "instant_bloom", chartTitle: "一瞬花绽", difficulty: "Hard", difficultyLevel: 9.0, goalText: "一瞬满开 · 达成 945,000 分", targetScore: 945000 },
  { chartId: "starry_sky", chartTitle: "繁星", difficulty: "Expert", difficultyLevel: 9.5, goalText: "星穹极境 · 达成 940,000 分", targetScore: 940000 },
];

export function computeDailyChallenge(dateUtc8?: string): DailyChallengeSpec {
  const resolved = (dateUtc8 ?? "").trim() || new Date(Date.now() + 8 * 3600_000).toISOString().slice(0, 10);
  if (!/^\d{4}-\d{2}-\d{2}$/.test(resolved)) {
    throw new Error("Invalid date format; expected YYYY-MM-DD.");
  }
  let h = 2166136261 >>> 0;
  for (let i = 0; i < resolved.length; i++) {
    h ^= resolved.charCodeAt(i) & 0xff;
    h = Math.imul(h, 16777619) >>> 0;
  }
  const idx = h % 12;
  return {
    date: resolved,
    ...DAILY_CHALLENGE_POOL[idx],
  };
}

interface SeasonPoolItem {
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
}

const VANGUARD_SEASON_POOL: ReadonlyArray<SeasonPoolItem> = [
  { chartId: "petal_storm_vip", chartTitle: "Petal Storm: Overdrive [VIP]", difficulty: "Normal", difficultyLevel: 6.5 },
  { chartId: "sakura_sprint_vip", chartTitle: "樱语疾行 · 极速", difficulty: "Normal", difficultyLevel: 6.5 },
  { chartId: "sakura_breath", chartTitle: "樱落呼吸", difficulty: "Hard", difficultyLevel: 7.5 },
  { chartId: "instant_bloom_vip", chartTitle: "一瞬花绽 · 里绽", difficulty: "Hard", difficultyLevel: 8.5 },
  { chartId: "sakura_breath_vip", chartTitle: "樱花落下的呼吸 · 终章", difficulty: "Hard", difficultyLevel: 9.0 },
  { chartId: "distant_star_vip", chartTitle: "遥远的星 · 回响", difficulty: "Hard", difficultyLevel: 9.5 },
];

const MID_SEASON_POOL: ReadonlyArray<SeasonPoolItem> = [
  { chartId: "sakura_sprint_vip", chartTitle: "樱语疾行 · 极速", difficulty: "Hard", difficultyLevel: 10.0 },
  { chartId: "petal_storm_vip", chartTitle: "Petal Storm: Overdrive [VIP]", difficulty: "Hard", difficultyLevel: 10.5 },
  { chartId: "shattered_threshold_omega", chartTitle: "破碎阈值 · Ω", difficulty: "Hard", difficultyLevel: 11.0 },
  { chartId: "sidechain_dream", chartTitle: "樱落侧链梦", difficulty: "Expert", difficultyLevel: 11.5 },
  { chartId: "instant_bloom_vip", chartTitle: "一瞬花绽 · 里绽", difficulty: "Expert", difficultyLevel: 12.0 },
  { chartId: "sakura_breath_vip", chartTitle: "樱花落下的呼吸 · 终章", difficulty: "Expert", difficultyLevel: 12.5 },
];

const BOSS_SEASON_POOL: ReadonlyArray<SeasonPoolItem> = [
  { chartId: "sakura_sprint", chartTitle: "樱语疾行", difficulty: "Expert", difficultyLevel: 13.0 },
  { chartId: "sakura_sprint_vip", chartTitle: "樱语疾行 · 极速", difficulty: "Expert", difficultyLevel: 13.5 },
  { chartId: "shattered_threshold_omega", chartTitle: "破碎阈值 · Ω", difficulty: "Expert", difficultyLevel: 13.8 },
  { chartId: "petal_storm_vip", chartTitle: "Petal Storm: Overdrive [VIP]", difficulty: "Expert", difficultyLevel: 14.5 },
  { chartId: "petal_storm_overdrive", chartTitle: "Petal Storm: Overdrive", difficulty: "Expert", difficultyLevel: 14.5 },
  { chartId: "shattered_threshold_omega", chartTitle: "破碎阈值 · Ω", difficulty: "Master", difficultyLevel: 15.0 },
];

const SEASON_THEMES: ReadonlyArray<string> = [
  "镜花幻月 · Mirror Blossom",
  "绯霞神乐 · Crimson Kagura",
  "星穹夜樱 · Astral Sakura",
  "流光剑心 · Luminous Blade",
  "霜月绝响 · Frostmoon Echo",
  "极星满开 · Zenith Bloom",
];

function fnv1a32Utf8(text: string): number {
  let h = 2166136261 >>> 0;
  const buf = Buffer.from(text, "utf8");
  for (let i = 0; i < buf.length; i++) {
    h ^= buf[i];
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

export function resolveSeasonIdUtc8(seasonIdUtc8?: string): string {
  const trimmed = (seasonIdUtc8 ?? "").trim();
  if (!trimmed) {
    return new Date(Date.now() + 8 * 3600_000).toISOString().slice(0, 7);
  }
  if (!/^\d{4}-(0[1-9]|1[0-2])$/.test(trimmed)) {
    throw new Error("Invalid season_id format; expected YYYY-MM.");
  }
  return trimmed;
}

export function computeSeasonSpec(seasonIdUtc8?: string): {
  seasonId: string;
  themeTitle: string;
  stages: [SeasonStageSpec, SeasonStageSpec, SeasonStageSpec];
} {
  const resolved = resolveSeasonIdUtc8(seasonIdUtc8);
  const year = Number(resolved.slice(0, 4));
  const month = Number(resolved.slice(5, 7));
  const themeIdx = (((year * 12 + (month - 1)) % 6) + 6) % 6;
  const chosenIds: string[] = [];
  const pick = (
    pool: ReadonlyArray<SeasonPoolItem>,
    slotIndex: number,
    slotRole: "vanguard" | "mid" | "boss",
    roleTitle: string,
    maxStageSP: number
  ): SeasonStageSpec => {
    const h = fnv1a32Utf8(`${resolved}#${slotRole}`);
    let idx = h % pool.length;
    for (let step = 0; step < pool.length; step++) {
      const cand = (idx + step) % pool.length;
      if (!chosenIds.includes(pool[cand].chartId)) {
        idx = cand;
        break;
      }
    }
    chosenIds.push(pool[idx].chartId);
    return {
      slotIndex,
      slotRole,
      roleTitle,
      chartId: pool[idx].chartId,
      chartTitle: pool[idx].chartTitle,
      difficulty: pool[idx].difficulty,
      difficultyLevel: pool[idx].difficultyLevel,
      maxStageSP,
    };
  };
  const s0 = pick(VANGUARD_SEASON_POOL, 0, "vanguard", "先锋课题", 1000);
  const s1 = pick(MID_SEASON_POOL, 1, "mid", "中坚课题", 1150);
  const s2 = pick(BOSS_SEASON_POOL, 2, "boss", "大将课题", 1350);
  return {
    seasonId: resolved,
    themeTitle: SEASON_THEMES[themeIdx],
    stages: [s0, s1, s2],
  };
}

export function calculateStageSP(
  sub: Pick<ScoreSubmission, "score" | "accuracy" | "isFullCombo" | "isAllPerfect" | "synergyCount" | "assisted" | "playbackRate">,
  chartLevel: number,
  slotIndex: number
): number {
  const rate = sub.playbackRate !== undefined ? Number(sub.playbackRate) : 1.0;
  if (sub.assisted || !Number.isFinite(rate) || Math.abs(rate - 1.0) >= 0.01) return 0;
  const maxSP = slotIndex === 0 ? 1000 : slotIndex === 1 ? 1150 : slotIndex === 2 ? 1350 : 0;
  if (maxSP <= 0 || sub.score <= 0 || sub.accuracy <= 0) return 0;

  const acc = Math.max(0, Math.min(100, Number(sub.accuracy)));
  let accFactor = 0;
  if (acc < 70) accFactor = 0.25 * (acc / 70.0);
  else if (acc < 90) accFactor = 0.25 + 0.35 * ((acc - 70.0) / 20.0);
  else if (acc < 95) accFactor = 0.60 + 0.16 * ((acc - 90.0) / 5.0);
  else if (acc < 98) accFactor = 0.76 + 0.12 * ((acc - 95.0) / 3.0);
  else if (acc < 99.5) accFactor = 0.88 + 0.07 * ((acc - 98.0) / 1.5);
  else accFactor = 0.95 + 0.05 * ((acc - 99.5) / 0.5);

  const scoreNorm = Math.max(0, Math.min(1, Number(sub.score) / 1_000_000.0));
  const slotMin = slotIndex === 0 ? 6.0 : slotIndex === 1 ? 9.5 : 12.5;
  const slotSpan = slotIndex === 0 ? 3.5 : slotIndex === 1 ? 3.0 : 2.5;
  const lvNorm = Math.max(0, Math.min(1, (Number(chartLevel) - slotMin) / slotSpan));
  const comboBonus = sub.isAllPerfect ? 0.030 : sub.isFullCombo ? 0.015 : 0.0;
  const synBonus = Math.min(0.010, Math.max(0, Number(sub.synergyCount || 0)) * 0.0005);
  const ratio = Math.max(0, Math.min(1, 0.845 * accFactor + 0.125 * scoreNorm + comboBonus + 0.008 * lvNorm + synBonus));
  return Math.max(0, Math.min(maxSP, Math.round(ratio * maxSP)));
}

export function evaluateSeasonTier(totalSP: number): {
  tierIndex: number;
  rankTier: string;
  nextTierName: string;
  nextTierThreshold: number;
  spToNextTier: number;
} {
  const sp = Math.max(0, Math.trunc(totalSP || 0));
  if (sp < 1200) return { tierIndex: 1, rankTier: "青铜樱", nextTierName: "白银月", nextTierThreshold: 1200, spToNextTier: 1200 - sp };
  if (sp < 1900) return { tierIndex: 2, rankTier: "白银月", nextTierName: "黄金华", nextTierThreshold: 1900, spToNextTier: 1900 - sp };
  if (sp < 2500) return { tierIndex: 3, rankTier: "黄金华", nextTierName: "绯红霞", nextTierThreshold: 2500, spToNextTier: 2500 - sp };
  if (sp < 3050) return { tierIndex: 4, rankTier: "绯红霞", nextTierName: "极星神乐", nextTierThreshold: 3050, spToNextTier: 3050 - sp };
  return { tierIndex: 5, rankTier: "极星神乐", nextTierName: "", nextTierThreshold: 3500, spToNextTier: 0 };
}

function extractUniqueChartIds(col: Record<string, unknown>): string[] {
  const raw = Array.isArray(col.chart_ids) ? col.chart_ids : Array.isArray(col.chartIds) ? col.chartIds : [];
  const seen = new Set<string>();
  const out: string[] = [];
  for (const item of raw) {
    if (typeof item === "string" && item.trim().length > 0) {
      const id = item.trim();
      if (!seen.has(id)) {
        seen.add(id);
        out.push(id);
      }
    }
  }
  return out;
}

function mergeCollectionsById(
  existingCols: unknown[],
  incomingCols: unknown[],
  incomingIsNewerOrTie: boolean
): Array<Record<string, unknown>> {
  const mergedMap = new Map<string, Record<string, unknown>>();
  for (const raw of existingCols) {
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) continue;
    const col = raw as Record<string, unknown>;
    const id = typeof col.id === "string" ? col.id.trim() : "";
    if (!id) continue;
    const copy: Record<string, unknown> = { ...col, id, chart_ids: extractUniqueChartIds(col) };
    mergedMap.set(id, copy);
  }
  for (const raw of incomingCols) {
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) continue;
    const col = raw as Record<string, unknown>;
    const id = typeof col.id === "string" ? col.id.trim() : "";
    if (!id) continue;
    const incomingIds = extractUniqueChartIds(col);
    const existing = mergedMap.get(id);
    if (!existing) {
      mergedMap.set(id, { ...col, id, chart_ids: incomingIds });
      continue;
    }
    const existingIds = extractUniqueChartIds(existing);
    const unionSet = new Set<string>(existingIds);
    const unionIds = [...existingIds];
    for (const cid of incomingIds) {
      if (!unionSet.has(cid)) {
        unionSet.add(cid);
        unionIds.push(cid);
      }
    }
    const exTs = Number(existing.updated_at ?? existing.updatedAt ?? 0);
    const inTs = Number(col.updated_at ?? col.updatedAt ?? 0);
    const preferIncomingMeta = inTs > exTs || (inTs === exTs && incomingIsNewerOrTie);
    const base: Record<string, unknown> = preferIncomingMeta ? { ...existing, ...col } : { ...col, ...existing };
    base.id = id;
    base.chart_ids = unionIds;
    if ("updated_at" in existing || "updated_at" in col) {
      base.updated_at = Math.max(exTs, inTs);
    }
    mergedMap.set(id, base);
  }
  return Array.from(mergedMap.values());
}

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

export function computeChartRating(
  level: number,
  accuracy: number,
  isFullCombo: boolean,
  isAllPerfect: boolean,
  synergyCount = 0
): number {
  const L = Math.max(0, level || 0);
  const acc = Math.max(0, Math.min(100, accuracy || 0));
  if (L <= 0) return 0;

  let base = 0;
  if (acc >= 100) {
    base = L + 2.0;
  } else if (acc >= 99.5) {
    base = L + 1.5 + (acc - 99.5) * 1.0;
  } else if (acc >= 98.0) {
    base = L + 1.0 + (acc - 98.0) / 3.0;
  } else if (acc >= 95.0) {
    base = L + (acc - 95.0) / 3.0;
  } else if (acc >= 90.0) {
    base = Math.max(0, L - 1.0 + (acc - 90.0) / 5.0);
  } else {
    base = Math.max(0, (L - 1.0) * Math.pow(acc / 90.0, 2));
  }

  const bonus =
    (isAllPerfect ? 0.12 : isFullCombo ? 0.06 : 0) +
    Math.min(0.06, Math.max(0, synergyCount || 0) * 0.002);
  return base + bonus;
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

export const ARENA_QUICK_CHAT_PHRASES: ReadonlyArray<string> = [
  "请多指教！",
  "准备好了，随时开局！",
  "稍等，正在同步房间谱面~",
  "这首定数好高，手下留情！",
  "精彩演奏！",
  "再来一局！",
];

export class SqliteStorageAdapter implements IStorageAdapter {
  public readonly mode: "sqlite" | "postgres" = "sqlite";
  public readonly db: DatabaseSync;
  public readonly dataDir: string;
  private readonly replaysDir: string;
  private readonly chartsDir: string;
  private readonly chatCooldownMs = new Map<string, number>();

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
        dan_badge TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS auth_sessions (
        id TEXT PRIMARY KEY,
        user_id TEXT NOT NULL,
        refresh_token_hash TEXT NOT NULL UNIQUE,
        device_name TEXT NOT NULL DEFAULT '',
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        revoked_at INTEGER DEFAULT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
      );
      CREATE INDEX IF NOT EXISTS idx_auth_sessions_user_id ON auth_sessions(user_id);
      CREATE INDEX IF NOT EXISTS idx_auth_sessions_refresh_hash ON auth_sessions(refresh_token_hash);
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
      CREATE TABLE IF NOT EXISTS cloud_courses (
        user_id TEXT NOT NULL,
        course_id TEXT NOT NULL,
        badge_text TEXT NOT NULL DEFAULT '',
        dan_rank INTEGER NOT NULL DEFAULT 0,
        total_score INTEGER NOT NULL DEFAULT 0,
        avg_accuracy REAL NOT NULL DEFAULT 0,
        remaining_life REAL NOT NULL DEFAULT 0,
        cleared INTEGER NOT NULL DEFAULT 0,
        cleared_at INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (user_id, course_id)
      );
      CREATE TABLE IF NOT EXISTS daily_scores (
        id TEXT PRIMARY KEY,
        date_utc8 TEXT NOT NULL,
        user_id TEXT NOT NULL,
        chart_id TEXT NOT NULL,
        difficulty TEXT NOT NULL,
        score INTEGER NOT NULL,
        accuracy REAL NOT NULL,
        max_combo INTEGER NOT NULL,
        grade TEXT NOT NULL,
        pp REAL NOT NULL,
        is_full_combo INTEGER NOT NULL DEFAULT 0,
        is_all_perfect INTEGER NOT NULL DEFAULT 0,
        played_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_daily_scores_lb ON daily_scores(date_utc8, score DESC);
      CREATE TABLE IF NOT EXISTS season_scores (
        season_id        TEXT NOT NULL,
        user_id          TEXT NOT NULL,
        slot_index       INTEGER NOT NULL,
        chart_id         TEXT NOT NULL,
        difficulty       TEXT NOT NULL,
        difficulty_level REAL NOT NULL,
        score            INTEGER NOT NULL,
        accuracy         REAL NOT NULL,
        max_combo        INTEGER NOT NULL,
        grade            TEXT NOT NULL,
        is_full_combo    INTEGER NOT NULL DEFAULT 0,
        is_all_perfect   INTEGER NOT NULL DEFAULT 0,
        stage_sp         INTEGER NOT NULL,
        replay_data      TEXT,
        updated_at       INTEGER NOT NULL,
        PRIMARY KEY (season_id, user_id, slot_index),
        FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
      );
      CREATE INDEX IF NOT EXISTS idx_season_scores_season_user ON season_scores(season_id, user_id);
      CREATE TABLE IF NOT EXISTS arena_rooms (
        code TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        host_id TEXT NOT NULL,
        host_name TEXT NOT NULL,
        status TEXT NOT NULL DEFAULT 'waiting',
        revision INTEGER NOT NULL DEFAULT 1,
        chart_id TEXT NOT NULL,
        chart_title TEXT NOT NULL,
        difficulty TEXT NOT NULL,
        difficulty_level REAL NOT NULL DEFAULT 1.0,
        chart_hash TEXT NOT NULL DEFAULT '',
        market_chart_id TEXT NOT NULL DEFAULT '',
        package_size INTEGER NOT NULL DEFAULT 0,
        messages_json TEXT NOT NULL DEFAULT '[]',
        next_msg_id INTEGER NOT NULL DEFAULT 1,
        started_at INTEGER NOT NULL DEFAULT 0,
        updated_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_arena_rooms_updated ON arena_rooms(status, updated_at DESC);
      CREATE TABLE IF NOT EXISTS arena_players (
        room_code TEXT NOT NULL,
        user_id TEXT NOT NULL,
        username TEXT NOT NULL,
        dan_badge TEXT NOT NULL DEFAULT '',
        is_host INTEGER NOT NULL DEFAULT 0,
        ready INTEGER NOT NULL DEFAULT 0,
        finished INTEGER NOT NULL DEFAULT 0,
        connected INTEGER NOT NULL DEFAULT 1,
        last_seen_at INTEGER NOT NULL DEFAULT 0,
        joined_at INTEGER NOT NULL DEFAULT 0,
        score INTEGER NOT NULL DEFAULT 0,
        accuracy REAL NOT NULL DEFAULT 0,
        combo INTEGER NOT NULL DEFAULT 0,
        max_combo INTEGER NOT NULL DEFAULT 0,
        grade TEXT NOT NULL DEFAULT 'D',
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (room_code, user_id)
      );
    `);
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS user_follows (
        follower_id TEXT NOT NULL,
        target_id   TEXT NOT NULL,
        created_at  INTEGER NOT NULL,
        PRIMARY KEY (follower_id, target_id),
        FOREIGN KEY (follower_id) REFERENCES users(id) ON DELETE CASCADE,
        FOREIGN KEY (target_id)   REFERENCES users(id) ON DELETE CASCADE
      );
      CREATE INDEX IF NOT EXISTS idx_user_follows_follower ON user_follows(follower_id, created_at DESC);
      CREATE INDEX IF NOT EXISTS idx_user_follows_target   ON user_follows(target_id);
    `);
    const userCols = this.db.prepare("PRAGMA table_info(users)").all() as Array<{ name?: string }>;
    if (!userCols.some((c) => c.name === "dan_badge")) {
      this.db.exec("ALTER TABLE users ADD COLUMN dan_badge TEXT NOT NULL DEFAULT ''");
    }
    if (!userCols.some((c) => c.name === "bio")) {
      this.db.exec("ALTER TABLE users ADD COLUMN bio TEXT NOT NULL DEFAULT ''");
    }
    if (!userCols.some((c) => c.name === "featured_title")) {
      this.db.exec("ALTER TABLE users ADD COLUMN featured_title TEXT NOT NULL DEFAULT ''");
    }
    const roomCols = this.db.prepare("PRAGMA table_info(arena_rooms)").all() as Array<{ name?: string }>;
    if (!roomCols.some((c) => c.name === "revision")) {
      this.db.exec("ALTER TABLE arena_rooms ADD COLUMN revision INTEGER NOT NULL DEFAULT 1");
    }
    if (!roomCols.some((c) => c.name === "market_chart_id")) {
      this.db.exec("ALTER TABLE arena_rooms ADD COLUMN market_chart_id TEXT NOT NULL DEFAULT ''");
    }
    if (!roomCols.some((c) => c.name === "package_size")) {
      this.db.exec("ALTER TABLE arena_rooms ADD COLUMN package_size INTEGER NOT NULL DEFAULT 0");
    }
    if (!roomCols.some((c) => c.name === "messages_json")) {
      this.db.exec("ALTER TABLE arena_rooms ADD COLUMN messages_json TEXT NOT NULL DEFAULT '[]'");
    }
    if (!roomCols.some((c) => c.name === "next_msg_id")) {
      this.db.exec("ALTER TABLE arena_rooms ADD COLUMN next_msg_id INTEGER NOT NULL DEFAULT 1");
    }
    const playerCols = this.db.prepare("PRAGMA table_info(arena_players)").all() as Array<{ name?: string }>;
    if (!playerCols.some((c) => c.name === "connected")) {
      this.db.exec("ALTER TABLE arena_players ADD COLUMN connected INTEGER NOT NULL DEFAULT 1");
    }
    if (!playerCols.some((c) => c.name === "last_seen_at")) {
      this.db.exec("ALTER TABLE arena_players ADD COLUMN last_seen_at INTEGER NOT NULL DEFAULT 0");
      this.db.exec("UPDATE arena_players SET last_seen_at = updated_at WHERE last_seen_at = 0");
    }
    if (!playerCols.some((c) => c.name === "joined_at")) {
      this.db.exec("ALTER TABLE arena_players ADD COLUMN joined_at INTEGER NOT NULL DEFAULT 0");
      this.db.exec("UPDATE arena_players SET joined_at = updated_at WHERE joined_at = 0");
    }
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
      "INSERT INTO users (id, username, password_hash, is_guest, dan_badge, created_at) VALUES (?, ?, '', 1, '', ?)"
    ).run(id, username, now);
    return { id, username, passwordHash: "", isGuest: true, danBadge: "", createdAt: now };
  }

  registerUser(username: string, passwordHash: string): UserRecord {
    const existing = this.findUserByUsername(username);
    if (existing) throw new Error("Username already exists.");
    const id = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    this.db.prepare(
      "INSERT INTO users (id, username, password_hash, is_guest, dan_badge, created_at) VALUES (?, ?, ?, 0, '', ?)"
    ).run(id, username, passwordHash, now);
    return { id, username, passwordHash, isGuest: false, danBadge: "", createdAt: now };
  }

  findUserByUsername(username: string): UserRecord | null {
    const row = this.db.prepare(
      "SELECT id, username, password_hash, is_guest, dan_badge, created_at FROM users WHERE username = ?"
    ).get(username) as Record<string, unknown> | undefined;
    if (!row) return null;
    return {
      id: String(row.id),
      username: String(row.username),
      passwordHash: String(row.password_hash),
      isGuest: Number(row.is_guest) !== 0,
      danBadge: String(row.dan_badge || ""),
      createdAt: Number(row.created_at),
    };
  }

  findUserById(id: string): UserRecord | null {
    const row = this.db.prepare(
      "SELECT id, username, password_hash, is_guest, dan_badge, bio, featured_title, created_at FROM users WHERE id = ?"
    ).get(id) as Record<string, unknown> | undefined;
    if (!row) return null;
    return {
      id: String(row.id),
      username: String(row.username),
      passwordHash: String(row.password_hash),
      isGuest: Number(row.is_guest) !== 0,
      danBadge: String(row.dan_badge || ""),
      bio: String(row.bio || ""),
      featuredTitle: String(row.featured_title || ""),
      createdAt: Number(row.created_at),
    };
  }

  createAuthSession(
    userId: string,
    refreshTokenHash: string,
    expiresAt: number,
    deviceName = ""
  ): AuthSessionRecord {
    const id = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    const cleanDevice = (deviceName || "").trim().slice(0, 128);
    const exp = Math.trunc(expiresAt);
    this.db.prepare(
      `INSERT INTO auth_sessions (id, user_id, refresh_token_hash, device_name, created_at, expires_at, revoked_at)
       VALUES (?, ?, ?, ?, ?, ?, NULL)`
    ).run(id, userId, refreshTokenHash, cleanDevice, now, exp);
    return {
      id,
      userId,
      refreshTokenHash,
      deviceName: cleanDevice,
      createdAt: now,
      expiresAt: exp,
      revokedAt: null,
    };
  }

  rotateAuthSession(
    oldRefreshTokenHash: string,
    newRefreshTokenHash: string,
    newExpiresAt: number,
    deviceName?: string
  ): UserRecord | null {
    if (!oldRefreshTokenHash || !newRefreshTokenHash) return null;
    const now = Math.floor(Date.now() / 1000);
    this.db.exec("BEGIN IMMEDIATE");
    try {
      const row = this.db.prepare(
        "SELECT id, user_id, device_name, expires_at, revoked_at FROM auth_sessions WHERE refresh_token_hash = ?"
      ).get(oldRefreshTokenHash) as
        | { id: string; user_id: string; device_name: string; expires_at: number; revoked_at: number | null }
        | undefined;

      if (!row || row.revoked_at != null || Number(row.expires_at) <= now) {
        this.db.exec("ROLLBACK");
        return null;
      }

      const user = this.findUserById(String(row.user_id));
      if (!user) {
        this.db.exec("ROLLBACK");
        return null;
      }

      const upd = this.db.prepare(
        "UPDATE auth_sessions SET revoked_at = ? WHERE id = ? AND revoked_at IS NULL"
      ).run(now, String(row.id));
      if (Number(upd.changes) !== 1) {
        this.db.exec("ROLLBACK");
        return null;
      }

      const resolvedDevice =
        deviceName !== undefined && deviceName.trim().length > 0
          ? deviceName.trim().slice(0, 128)
          : String(row.device_name || "");

      this.db.prepare(
        `INSERT INTO auth_sessions (id, user_id, refresh_token_hash, device_name, created_at, expires_at, revoked_at)
         VALUES (?, ?, ?, ?, ?, ?, NULL)`
      ).run(crypto.randomUUID(), user.id, newRefreshTokenHash, resolvedDevice, now, Math.trunc(newExpiresAt));

      this.db.exec("COMMIT");
      return user;
    } catch (err) {
      try {
        this.db.exec("ROLLBACK");
      } catch {}
      throw err;
    }
  }

  revokeAuthSession(refreshTokenHash: string): boolean {
    if (!refreshTokenHash) return false;
    const now = Math.floor(Date.now() / 1000);
    const res = this.db.prepare(
      "UPDATE auth_sessions SET revoked_at = ? WHERE refresh_token_hash = ? AND revoked_at IS NULL"
    ).run(now, refreshTokenHash);
    return Number(res.changes) > 0;
  }

  revokeUserAuthSessions(userId: string, exceptRefreshTokenHash?: string): number {
    if (!userId) return 0;
    const now = Math.floor(Date.now() / 1000);
    if (exceptRefreshTokenHash) {
      const res = this.db.prepare(
        "UPDATE auth_sessions SET revoked_at = ? WHERE user_id = ? AND revoked_at IS NULL AND refresh_token_hash != ?"
      ).run(now, userId, exceptRefreshTokenHash);
      return Number(res.changes);
    }
    const res = this.db.prepare(
      "UPDATE auth_sessions SET revoked_at = ? WHERE user_id = ? AND revoked_at IS NULL"
    ).run(now, userId);
    return Number(res.changes);
  }

  upgradeGuestUser(userId: string, username: string, passwordHash: string): UserRecord {
    const cleanUsername = username.trim();
    this.db.exec("BEGIN IMMEDIATE");
    try {
      const user = this.findUserById(userId);
      if (!user) {
        throw new Error("User not found.");
      }
      if (!user.isGuest) {
        throw new Error("Account is already registered.");
      }
      const conflict = this.db.prepare(
        "SELECT id FROM users WHERE username = ? COLLATE NOCASE AND id != ?"
      ).get(cleanUsername, userId) as { id: string } | undefined;
      if (conflict) {
        throw new Error("Username already exists.");
      }
      this.db.prepare(
        "UPDATE users SET username = ?, password_hash = ?, is_guest = 0 WHERE id = ?"
      ).run(cleanUsername, passwordHash, userId);
      this.db.prepare(
        "UPDATE market_charts SET uploader_name = ? WHERE uploader_id = ?"
      ).run(cleanUsername, userId);
      this.db.prepare(
        "UPDATE chart_ratings SET username = ? WHERE user_id = ?"
      ).run(cleanUsername, userId);
      this.db.prepare(
        "UPDATE arena_rooms SET host_name = ? WHERE host_id = ?"
      ).run(cleanUsername, userId);
      this.db.prepare(
        "UPDATE arena_players SET username = ? WHERE user_id = ?"
      ).run(cleanUsername, userId);
      this.db.exec("COMMIT");
      return this.findUserById(userId)!;
    } catch (err) {
      try {
        this.db.exec("ROLLBACK");
      } catch {}
      throw err;
    }
  }

  updateUserPassword(userId: string, passwordHash: string): boolean {
    if (!userId || !passwordHash) return false;
    const res = this.db.prepare(
      "UPDATE users SET password_hash = ? WHERE id = ? AND is_guest = 0"
    ).run(passwordHash, userId);
    return Number(res.changes) > 0;
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

  getUserSakuraRating(userId: string): number {
    const bestByChartDiff = new Map<string, number>();
    const recentRatings: number[] = [];

    const scoreRows = this.db.prepare(`
      SELECT chart_id, difficulty, difficulty_level, accuracy, synergy_count, is_full_combo, is_all_perfect
      FROM scores
      WHERE user_id = ?
      ORDER BY played_at DESC, rowid DESC
    `).all(userId) as Array<Record<string, unknown>>;

    for (const r of scoreRows) {
      const rating = computeChartRating(
        Number(r.difficulty_level),
        Number(r.accuracy),
        Number(r.is_full_combo) !== 0,
        Number(r.is_all_perfect) !== 0,
        Number(r.synergy_count || 0)
      );
      const key = `${String(r.chart_id)}::${String(r.difficulty)}`;
      const prev = bestByChartDiff.get(key);
      if (prev === undefined || rating > prev) {
        bestByChartDiff.set(key, rating);
      }
      if (recentRatings.length < 20) {
        recentRatings.push(rating);
      }
    }

    const cloudRows = this.db.prepare(`
      SELECT chart_id, difficulty, difficulty_level, accuracy, is_full_combo, is_all_perfect
      FROM cloud_best_scores
      WHERE user_id = ?
    `).all(userId) as Array<Record<string, unknown>>;

    for (const r of cloudRows) {
      const rating = computeChartRating(
        Number(r.difficulty_level),
        Number(r.accuracy),
        Number(r.is_full_combo) !== 0,
        Number(r.is_all_perfect) !== 0,
        0
      );
      const key = `${String(r.chart_id)}::${String(r.difficulty)}`;
      const prev = bestByChartDiff.get(key);
      if (prev === undefined || rating > prev) {
        bestByChartDiff.set(key, rating);
      }
    }

    if (bestByChartDiff.size === 0) {
      return 0;
    }

    const top30 = Array.from(bestByChartDiff.values())
      .sort((a, b) => b - a)
      .slice(0, 30);
    const b30Avg = top30.reduce((sum, v) => sum + v, 0) / top30.length;

    let rawR10 = b30Avg;
    if (recentRatings.length > 0) {
      const top10 = [...recentRatings].sort((a, b) => b - a).slice(0, 10);
      rawR10 = top10.reduce((sum, v) => sum + v, 0) / top10.length;
    }
    const r10Avg = Math.max(b30Avg * 0.85, rawR10);

    return Math.round((0.75 * b30Avg + 0.25 * r10Avg) * 100) / 100;
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

  getLeaderboard(
    chartId: string,
    difficulty: string,
    limit: number,
    currentUserId?: string,
    scope: "global" | "rivals" = "global"
  ): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  } {
    if (scope === "rivals" && !currentUserId) {
      return { entries: [], myRank: null };
    }
    let allowedUsers: Set<string> | null = null;
    if (scope === "rivals" && currentUserId) {
      allowedUsers = new Set<string>([currentUserId]);
      const rivalRows = this.db.prepare(
        "SELECT target_id FROM user_follows WHERE follower_id = ?"
      ).all(currentUserId) as Array<{ target_id: string }>;
      for (const rr of rivalRows) {
        allowedUsers.add(String(rr.target_id));
      }
    }

    const capped = Math.max(1, Math.min(100, limit || 50));
    const rows = this.db.prepare(`
      SELECT s.id, s.user_id, u.username, u.dan_badge, s.score, s.accuracy, s.max_combo, s.grade, s.pp,
             s.is_full_combo, s.is_all_perfect, s.has_replay, s.played_at
      FROM scores s
      JOIN users u ON u.id = s.user_id
      WHERE s.chart_id = ? AND s.difficulty = ?
      ORDER BY s.score DESC, s.accuracy DESC, s.played_at ASC
    `).all(chartId, difficulty) as Array<Record<string, unknown>>;

    const seenUsers = new Set<string>();
    const deduped: LeaderboardEntry[] = [];
    let uniqueRank = 0;
    let myRank: number | null = null;

    for (const r of rows) {
      const uid = String(r.user_id);
      if (allowedUsers && !allowedUsers.has(uid)) continue;
      if (seenUsers.has(uid)) continue;
      seenUsers.add(uid);
      uniqueRank++;
      if (currentUserId && uid === currentUserId) {
        myRank = uniqueRank;
      }
      if (deduped.length < capped) {
        deduped.push({
          rank: uniqueRank,
          scoreId: String(r.id),
          userId: uid,
          username: String(r.username),
          danBadge: String(r.dan_badge || ""),
          score: Number(r.score),
          accuracy: Number(r.accuracy),
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          pp: Number(r.pp),
          sakuraRating: this.getUserSakuraRating(uid),
          isFullCombo: Number(r.is_full_combo) !== 0,
          isAllPerfect: Number(r.is_all_perfect) !== 0,
          hasReplay: Number(r.has_replay) !== 0,
          playedAt: Number(r.played_at),
        });
      }
    }
    return { entries: deduped, myRank };
  }

  submitDailyScore(
    userId: string,
    sub: ScoreSubmission,
    dateUtc8?: string
  ): { scoreId: string; date: string; pp: number; rank: number; newBest: boolean } {
    const spec = computeDailyChallenge(dateUtc8);
    validateScoreSubmission(sub);
    if (sub.chartId !== spec.chartId || sub.difficulty !== spec.difficulty) {
      throw new Error(`Daily challenge mismatch: expected ${spec.chartId} (${spec.difficulty}) for ${spec.date}.`);
    }

    const prev = this.db.prepare(
      "SELECT MAX(score) AS best FROM daily_scores WHERE date_utc8 = ? AND user_id = ?"
    ).get(spec.date, userId) as { best: number | null } | undefined;
    const newBest = prev?.best == null || sub.score > Number(prev.best);

    const pp = computeAuthoritativePP({ ...sub, difficultyLevel: spec.difficultyLevel });
    const scoreId = crypto.randomUUID();
    const now = Math.floor(Date.now() / 1000);
    if (sub.replayData && sub.replayData.length > 0) {
      fs.writeFileSync(path.join(this.replaysDir, `${scoreId}.skr`), sub.replayData, "utf-8");
    }

    this.db.prepare(`
      INSERT INTO daily_scores (
        id, date_utc8, user_id, chart_id, difficulty,
        score, accuracy, max_combo, grade, pp,
        is_full_combo, is_all_perfect, played_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `).run(
      scoreId,
      spec.date,
      userId,
      spec.chartId,
      spec.difficulty,
      sub.score,
      sub.accuracy,
      sub.maxCombo,
      sub.grade || "D",
      pp,
      sub.isFullCombo ? 1 : 0,
      sub.isAllPerfect ? 1 : 0,
      now
    );

    const lb = this.getDailyLeaderboard(spec.date, 1000, userId);
    return { scoreId, date: spec.date, pp, rank: lb.myRank ?? 1, newBest };
  }

  getDailyLeaderboard(
    dateUtc8: string,
    limit: number,
    currentUserId?: string
  ): { entries: LeaderboardEntry[]; myRank: number | null } {
    const spec = computeDailyChallenge(dateUtc8);
    const capped = Math.max(1, Math.min(100, limit || 50));
    const rows = this.db.prepare(`
      SELECT d.id, d.user_id, u.username, u.dan_badge, d.score, d.accuracy, d.max_combo, d.grade, d.pp,
             d.is_full_combo, d.is_all_perfect, d.played_at
      FROM daily_scores d
      JOIN users u ON u.id = d.user_id
      WHERE d.date_utc8 = ?
      ORDER BY d.score DESC, d.accuracy DESC, d.played_at ASC
    `).all(spec.date) as Array<Record<string, unknown>>;

    const seenUsers = new Set<string>();
    const deduped: LeaderboardEntry[] = [];
    let uniqueRank = 0;
    let myRank: number | null = null;

    for (const r of rows) {
      const uid = String(r.user_id);
      if (seenUsers.has(uid)) continue;
      seenUsers.add(uid);
      uniqueRank++;
      if (currentUserId && uid === currentUserId) {
        myRank = uniqueRank;
      }
      if (deduped.length < capped) {
        const sid = String(r.id);
        const hasReplay = fs.existsSync(path.join(this.replaysDir, `${sid}.skr`));
        deduped.push({
          rank: uniqueRank,
          scoreId: sid,
          userId: uid,
          username: String(r.username),
          danBadge: String(r.dan_badge || ""),
          score: Number(r.score),
          accuracy: Number(r.accuracy),
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          pp: Number(r.pp),
          sakuraRating: this.getUserSakuraRating(uid),
          isFullCombo: Number(r.is_full_combo) !== 0,
          isAllPerfect: Number(r.is_all_perfect) !== 0,
          hasReplay,
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
      courseRecords?: CloudCourseRecord[];
      settings?: Record<string, unknown>;
      settingsUpdatedAt?: number;
    }
  ): { scoresMerged: number; achievementsMerged: number; coursesMerged: number; snapshot: CloudSyncSnapshot } {
    const now = Math.floor(Date.now() / 1000);
    let scoresMerged = 0;
    let achievementsMerged = 0;
    let coursesMerged = 0;

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

    const selectCourse = this.db.prepare(
      "SELECT badge_text, dan_rank, total_score, avg_accuracy, remaining_life, cleared, cleared_at FROM cloud_courses WHERE user_id = ? AND course_id = ?"
    );
    const upsertCourse = this.db.prepare(`
      INSERT INTO cloud_courses (
        user_id, course_id, badge_text, dan_rank, total_score,
        avg_accuracy, remaining_life, cleared, cleared_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT(user_id, course_id) DO UPDATE SET
        badge_text = excluded.badge_text,
        dan_rank = excluded.dan_rank,
        total_score = excluded.total_score,
        avg_accuracy = excluded.avg_accuracy,
        remaining_life = excluded.remaining_life,
        cleared = excluded.cleared,
        cleared_at = excluded.cleared_at
    `);

    for (const cr of payload.courseRecords || []) {
      if (!cr || typeof cr.courseId !== "string" || !cr.courseId.trim()) continue;
      const cid = cr.courseId.trim();
      const inBadge = (cr.badgeText || "").trim();
      const inRank = Math.max(0, Math.trunc(cr.danRank || 0));
      const inScore = Math.max(0, Math.trunc(cr.totalScore || 0));
      const inAcc = Math.max(0, Number(cr.avgAccuracy || 0));
      const inLife = Math.max(0, Number(cr.remainingLife || 0));
      const inCleared = cr.cleared ? 1 : 0;
      const inClearedAt = inCleared ? (cr.clearedAt && cr.clearedAt > 0 ? Math.trunc(cr.clearedAt) : now) : 0;

      const prevCourse = selectCourse.get(userId, cid) as {
        badge_text: string;
        dan_rank: number;
        total_score: number;
        avg_accuracy: number;
        remaining_life: number;
        cleared: number;
        cleared_at: number;
      } | undefined;

      if (!prevCourse) {
        upsertCourse.run(userId, cid, inBadge, inRank, inScore, inAcc, inLife, inCleared, inClearedAt);
        coursesMerged++;
      } else {
        const prevCleared = Number(prevCourse.cleared) !== 0 ? 1 : 0;
        const mergedCleared = Math.max(prevCleared, inCleared);
        const incomingWinsPerf =
          inCleared > prevCleared ||
          (inCleared === prevCleared &&
            (inScore > Number(prevCourse.total_score) ||
              (inScore === Number(prevCourse.total_score) && inAcc > Number(prevCourse.avg_accuracy))));
        const mergedBadge = inBadge || String(prevCourse.badge_text || "");
        const mergedRank = Math.max(Number(prevCourse.dan_rank || 0), inRank);
        const mergedScore = incomingWinsPerf ? inScore : Number(prevCourse.total_score);
        const mergedAcc = incomingWinsPerf ? inAcc : Number(prevCourse.avg_accuracy);
        const mergedLife = incomingWinsPerf ? inLife : Number(prevCourse.remaining_life);
        let mergedClearedAt = Number(prevCourse.cleared_at || 0);
        if (mergedCleared) {
          if (mergedClearedAt <= 0) mergedClearedAt = inClearedAt || now;
          else if (inClearedAt > 0 && inClearedAt < mergedClearedAt) mergedClearedAt = inClearedAt;
        }
        if (
          incomingWinsPerf ||
          mergedCleared !== prevCleared ||
          mergedRank !== Number(prevCourse.dan_rank || 0) ||
          mergedBadge !== String(prevCourse.badge_text || "") ||
          mergedClearedAt !== Number(prevCourse.cleared_at || 0)
        ) {
          upsertCourse.run(userId, cid, mergedBadge, mergedRank, mergedScore, mergedAcc, mergedLife, mergedCleared, mergedClearedAt);
          coursesMerged++;
        }
      }
    }

    if ((payload.courseRecords?.length ?? 0) > 0) {
      const bestDan = this.db.prepare(`
        SELECT badge_text
        FROM cloud_courses
        WHERE user_id = ? AND cleared != 0 AND badge_text != ''
        ORDER BY dan_rank DESC, total_score DESC, cleared_at ASC
        LIMIT 1
      `).get(userId) as { badge_text?: string } | undefined;
      if (bestDan?.badge_text) {
        this.db.prepare("UPDATE users SET dan_badge = ? WHERE id = ?").run(String(bestDan.badge_text), userId);
      }
    }

    if (payload.settings && typeof payload.settings === "object" && Object.keys(payload.settings).length > 0) {
      const incomingTs = payload.settingsUpdatedAt && payload.settingsUpdatedAt > 0 ? Math.trunc(payload.settingsUpdatedAt) : now;
      const prevSet = this.db.prepare("SELECT settings_json, updated_at FROM cloud_settings WHERE user_id = ?").get(userId) as { settings_json: string; updated_at: number } | undefined;
      let existingObj: Record<string, unknown> = {};
      if (prevSet) {
        try { existingObj = JSON.parse(String(prevSet.settings_json || "{}")); } catch { existingObj = {}; }
      }
      const prevUpdatedAt = prevSet ? Number(prevSet.updated_at || 0) : 0;
      const incomingIsNewerOrTie = !prevSet || incomingTs >= prevUpdatedAt;
      const bothHaveCollections = Array.isArray(existingObj.collections) && Array.isArray(payload.settings.collections);

      if (incomingIsNewerOrTie) {
        const mergedObj: Record<string, unknown> = { ...existingObj, ...payload.settings };
        if (bothHaveCollections) {
          mergedObj.collections = mergeCollectionsById(
            existingObj.collections as unknown[],
            payload.settings.collections as unknown[],
            true
          );
        } else if (Array.isArray(payload.settings.collections)) {
          mergedObj.collections = mergeCollectionsById([], payload.settings.collections as unknown[], true);
        }
        this.db.prepare(`
          INSERT INTO cloud_settings (user_id, settings_json, updated_at)
          VALUES (?, ?, ?)
          ON CONFLICT(user_id) DO UPDATE SET settings_json = excluded.settings_json, updated_at = excluded.updated_at
        `).run(userId, JSON.stringify(mergedObj), Math.max(prevUpdatedAt, incomingTs));
      } else if (Array.isArray(payload.settings.collections)) {
        const mergedObj: Record<string, unknown> = { ...existingObj };
        mergedObj.collections = mergeCollectionsById(
          Array.isArray(existingObj.collections) ? (existingObj.collections as unknown[]) : [],
          payload.settings.collections as unknown[],
          false
        );
        this.db.prepare(`
          INSERT INTO cloud_settings (user_id, settings_json, updated_at)
          VALUES (?, ?, ?)
          ON CONFLICT(user_id) DO UPDATE SET settings_json = excluded.settings_json, updated_at = excluded.updated_at
        `).run(userId, JSON.stringify(mergedObj), prevUpdatedAt);
      }
    }

    return {
      scoresMerged,
      achievementsMerged,
      coursesMerged,
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

    const courseRows = this.db.prepare(
      "SELECT course_id, badge_text, dan_rank, total_score, avg_accuracy, remaining_life, cleared, cleared_at FROM cloud_courses WHERE user_id = ? ORDER BY dan_rank ASC, course_id ASC"
    ).all(userId) as Array<Record<string, unknown>>;

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
      courseRecords: courseRows.map((cr) => ({
        courseId: String(cr.course_id),
        badgeText: String(cr.badge_text || ""),
        danRank: Number(cr.dan_rank || 0),
        totalScore: Number(cr.total_score || 0),
        avgAccuracy: Number(cr.avg_accuracy || 0),
        remainingLife: Number(cr.remaining_life || 0),
        cleared: Number(cr.cleared) !== 0,
        clearedAt: Number(cr.cleared_at || 0),
      })),
      settings,
      settingsUpdatedAt,
    };
  }

  private generateArenaRoomCode(): string {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (let attempt = 0; attempt < 32; attempt++) {
      const bytes = crypto.randomBytes(6);
      let code = "";
      for (let i = 0; i < 6; i++) {
        code += alphabet[bytes[i] % alphabet.length];
      }
      const exists = this.db.prepare("SELECT code FROM arena_rooms WHERE code = ?").get(code);
      if (!exists) return code;
    }
    throw new Error("Failed to allocate unique arena room code.");
  }

  private resolveNowSec(nowSec?: number): number {
    return nowSec !== undefined && Number.isFinite(nowSec) ? Math.floor(nowSec) : Math.floor(Date.now() / 1000);
  }

  private resolveMarketChartLink(
    marketChartIdInput?: string,
    chartHash?: string,
    chartId?: string
  ): { marketChartId: string; packageSize: number } {
    const explicitId = (marketChartIdInput || "").trim();
    if (explicitId) {
      const row = this.db.prepare(`
        SELECT id, package_size
        FROM market_charts
        WHERE id = ? OR chart_slug = ?
        ORDER BY CASE WHEN id = ? THEN 0 ELSE 1 END, updated_at DESC
        LIMIT 1
      `).get(explicitId, explicitId, explicitId) as { id: string; package_size: number } | undefined;
      if (row) {
        return { marketChartId: String(row.id), packageSize: Math.max(0, Number(row.package_size || 0)) };
      }
    }

    const cleanHash = (chartHash || "").trim();
    if (cleanHash) {
      const row = this.db.prepare(`
        SELECT id, package_size
        FROM market_charts
        WHERE content_hash = ? OR package_sha256 = ?
        ORDER BY CASE WHEN content_hash = ? THEN 0 ELSE 1 END, updated_at DESC
        LIMIT 1
      `).get(cleanHash, cleanHash, cleanHash) as { id: string; package_size: number } | undefined;
      if (row) {
        return { marketChartId: String(row.id), packageSize: Math.max(0, Number(row.package_size || 0)) };
      }
    }

    const cleanChartId = (chartId || "").trim();
    if (cleanChartId) {
      const row = this.db.prepare(`
        SELECT id, package_size
        FROM market_charts
        WHERE id = ? OR chart_slug = ?
        ORDER BY CASE WHEN id = ? THEN 0 ELSE 1 END, updated_at DESC
        LIMIT 1
      `).get(cleanChartId, cleanChartId, cleanChartId) as { id: string; package_size: number } | undefined;
      if (row) {
        return { marketChartId: String(row.id), packageSize: Math.max(0, Number(row.package_size || 0)) };
      }
    }

    return { marketChartId: explicitId, packageSize: 0 };
  }

  private sweepArenaRoomInternal(
    normCode: string,
    nowSec: number,
    callerUserId?: string,
    bumpRevisionIfChanged = true
  ): { exists: boolean; changed: boolean } {
    const room = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!room) return { exists: false, changed: false };

    const pRows = this.db.prepare(`
      SELECT user_id, username, is_host, ready, finished, connected, last_seen_at, joined_at, updated_at, rowid AS row_id
      FROM arena_players
      WHERE room_code = ?
      ORDER BY joined_at ASC, rowid ASC
    `).all(normCode) as Array<Record<string, unknown>>;

    if (pRows.length === 0) {
      return { exists: true, changed: false };
    }

    let changed = false;
    const expiredIds: string[] = [];
    for (const p of pRows) {
      const lastSeen = Number(p.last_seen_at || 0) > 0 ? Number(p.last_seen_at) : Number(p.updated_at || 0);
      if (nowSec - lastSeen > 60) {
        expiredIds.push(String(p.user_id));
      }
    }

    if (expiredIds.length > 0) {
      const delStmt = this.db.prepare("DELETE FROM arena_players WHERE room_code = ? AND user_id = ?");
      for (const uid of expiredIds) {
        delStmt.run(normCode, uid);
        this.chatCooldownMs.delete(`${normCode}:${uid}`);
      }
      changed = true;
    }

    const remaining = pRows.filter((p) => !expiredIds.includes(String(p.user_id)));
    if (remaining.length === 0 && expiredIds.length > 0) {
      this.db.prepare("DELETE FROM arena_rooms WHERE code = ?").run(normCode);
      return { exists: false, changed: true };
    }

    interface RemainingMeta {
      userId: string;
      username: string;
      isHost: boolean;
      finished: boolean;
      connected: boolean;
      joinedAt: number;
      rowId: number;
    }
    const updatedRemaining: RemainingMeta[] = [];

    for (const p of remaining) {
      const uid = String(p.user_id);
      const prevConnected = Number(p.connected ?? 1) !== 0;
      const lastSeen = Number(p.last_seen_at || 0) > 0 ? Number(p.last_seen_at) : Number(p.updated_at || 0);
      const joinedAt = Number(p.joined_at || 0) > 0 ? Number(p.joined_at) : Number(p.updated_at || 0);
      let nextConnected = prevConnected;

      if (callerUserId && uid === callerUserId) {
        nextConnected = true;
        this.db.prepare(
          "UPDATE arena_players SET connected = 1, last_seen_at = ? WHERE room_code = ? AND user_id = ?"
        ).run(nowSec, normCode, uid);
        if (!prevConnected) {
          changed = true;
        }
      } else if (nowSec - lastSeen > 12) {
        nextConnected = false;
        if (prevConnected) {
          this.db.prepare(
            "UPDATE arena_players SET connected = 0 WHERE room_code = ? AND user_id = ?"
          ).run(normCode, uid);
          changed = true;
        }
      }

      updatedRemaining.push({
        userId: uid,
        username: String(p.username),
        isHost: Number(p.is_host) !== 0,
        finished: Number(p.finished) !== 0,
        connected: nextConnected,
        joinedAt,
        rowId: Number(p.row_id || 0),
      });
    }

    const hasValidHost = updatedRemaining.some((p) => p.isHost && p.userId === String(room.host_id));
    if (!hasValidHost && updatedRemaining.length > 0) {
      const candidates = [...updatedRemaining].sort((a, b) => {
        if (a.connected !== b.connected) return a.connected ? -1 : 1;
        if (a.joinedAt !== b.joinedAt) return a.joinedAt - b.joinedAt;
        return a.rowId - b.rowId;
      });
      const nextHost = candidates[0];
      this.db.prepare(
        "UPDATE arena_players SET is_host = CASE WHEN user_id = ? THEN 1 ELSE 0 END, ready = CASE WHEN user_id = ? THEN 1 ELSE ready END WHERE room_code = ?"
      ).run(nextHost.userId, nextHost.userId, normCode);
      this.db.prepare(
        "UPDATE arena_rooms SET host_id = ?, host_name = ? WHERE code = ?"
      ).run(nextHost.userId, nextHost.username, normCode);
      changed = true;
    }

    if (expiredIds.length > 0 && String(room.status) === "playing" && updatedRemaining.every((p) => p.finished)) {
      this.db.prepare("UPDATE arena_rooms SET status = 'finished' WHERE code = ?").run(normCode);
      changed = true;
    }

    if (changed && bumpRevisionIfChanged) {
      this.db.prepare(
        "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
      ).run(nowSec, normCode);
    }

    return { exists: true, changed };
  }

  sweepArenaHeartbeats(nowSec?: number): number {
    const now = this.resolveNowSec(nowSec);
    const rows = this.db.prepare("SELECT code FROM arena_rooms").all() as Array<{ code: string }>;
    let affected = 0;
    for (const r of rows) {
      const res = this.sweepArenaRoomInternal(String(r.code), now, undefined, true);
      if (res.changed) affected++;
    }
    return affected;
  }

  private readArenaRoomDirect(normCode: string): ArenaRoomRecord | null {
    const row = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!row) return null;
    return this.rowToArenaRoom(row);
  }

  private rowToArenaRoom(row: Record<string, unknown>): ArenaRoomRecord {
    const code = String(row.code);
    const pRows = this.db.prepare(`
      SELECT user_id, username, dan_badge, is_host, ready, finished,
             connected, last_seen_at,
             score, accuracy, combo, max_combo, grade, updated_at
      FROM arena_players
      WHERE room_code = ?
      ORDER BY score DESC, accuracy DESC, updated_at ASC, rowid ASC
    `).all(code) as Array<Record<string, unknown>>;

    const players: ArenaPlayerRecord[] = pRows.map((p, idx) => {
      const rawLastSeen = Number(p.last_seen_at || 0);
      return {
        userId: String(p.user_id),
        username: String(p.username),
        danBadge: String(p.dan_badge || ""),
        isHost: Number(p.is_host) !== 0,
        ready: Number(p.ready) !== 0,
        finished: Number(p.finished) !== 0,
        connected: Number(p.connected ?? 1) !== 0,
        lastSeenAt: rawLastSeen > 0 ? rawLastSeen : Number(p.updated_at || 0),
        score: Number(p.score || 0),
        accuracy: Number(p.accuracy || 0),
        combo: Number(p.combo || 0),
        maxCombo: Number(p.max_combo || 0),
        grade: String(p.grade || "D"),
        placement: idx + 1,
      };
    });

    let messages: ArenaChatMessage[] = [];
    try {
      const parsed = JSON.parse(String(row.messages_json || "[]"));
      if (Array.isArray(parsed)) {
        messages = parsed.map((m) => {
          const obj = (m || {}) as Record<string, unknown>;
          return {
            id: Number(obj.id || 0),
            userId: String(obj.userId ?? obj.user_id ?? ""),
            username: String(obj.username ?? ""),
            phraseId: Number(obj.phraseId ?? obj.phrase_id ?? 0),
            text: String(obj.text ?? ""),
            sentAt: Number(obj.sentAt ?? obj.sent_at ?? 0),
          };
        });
      }
    } catch {
      messages = [];
    }

    const rawStatus = String(row.status || "waiting");
    const status: ArenaRoomStatus =
      rawStatus === "playing" || rawStatus === "finished" ? rawStatus : "waiting";

    return {
      code,
      name: String(row.name),
      hostId: String(row.host_id),
      hostName: String(row.host_name),
      status,
      revision: Math.max(1, Number(row.revision || 1)),
      chartId: String(row.chart_id),
      chartTitle: String(row.chart_title),
      difficulty: String(row.difficulty),
      difficultyLevel: Number(row.difficulty_level || 1.0),
      chartHash: String(row.chart_hash || ""),
      marketChartId: String(row.market_chart_id || ""),
      packageSize: Math.max(0, Number(row.package_size || 0)),
      startedAt: Number(row.started_at || 0),
      updatedAt: Number(row.updated_at || 0),
      players,
      messages,
    };
  }

  createArenaRoom(userId: string, input: ArenaCreateRoomInput, nowSec?: number): ArenaRoomRecord {
    const user = this.findUserById(userId);
    if (!user) throw new Error("User not found.");
    const chartId = (input.chartId || "").trim();
    if (!chartId) throw new Error("Missing chart_id.");
    const chartTitle = (input.chartTitle || "").trim() || chartId;
    const difficulty = (input.difficulty || "").trim() || "Normal";
    const difficultyLevel =
      Number.isFinite(input.difficultyLevel) && input.difficultyLevel > 0
        ? Number(input.difficultyLevel)
        : 1.0;
    const chartHash = (input.chartHash || "").trim();
    const linked = this.resolveMarketChartLink(input.marketChartId, chartHash, chartId);
    const name = (input.name || "").trim().slice(0, 48) || `${user.username}'s Room`;
    const code = this.generateArenaRoomCode();
    const now = this.resolveNowSec(nowSec);

    this.db.prepare(`
      INSERT INTO arena_rooms (
        code, name, host_id, host_name, status, revision,
        chart_id, chart_title, difficulty, difficulty_level, chart_hash,
        market_chart_id, package_size, messages_json, next_msg_id,
        started_at, updated_at
      ) VALUES (?, ?, ?, ?, 'waiting', 1, ?, ?, ?, ?, ?, ?, ?, '[]', 1, 0, ?)
    `).run(
      code,
      name,
      user.id,
      user.username,
      chartId,
      chartTitle,
      difficulty,
      difficultyLevel,
      chartHash,
      linked.marketChartId,
      linked.packageSize,
      now
    );

    this.db.prepare(`
      INSERT INTO arena_players (
        room_code, user_id, username, dan_badge, is_host, ready, finished,
        connected, last_seen_at, joined_at,
        score, accuracy, combo, max_combo, grade, updated_at
      ) VALUES (?, ?, ?, ?, 1, 1, 0, 1, ?, ?, 0, 0, 0, 0, 'D', ?)
    `).run(code, user.id, user.username, user.danBadge, now, now, now);

    return this.readArenaRoomDirect(code)!;
  }

  listArenaRooms(limit = 50, nowSec?: number): ArenaRoomRecord[] {
    const now = this.resolveNowSec(nowSec);
    const capped = Math.max(1, Math.min(100, Math.trunc(limit || 50)));
    const cutoff = now - 7200;
    const candidateCodes = this.db.prepare(`
      SELECT code FROM arena_rooms
      WHERE status IN ('waiting', 'playing') AND updated_at >= ?
    `).all(cutoff) as Array<{ code: string }>;
    for (const c of candidateCodes) {
      this.sweepArenaRoomInternal(String(c.code), now, undefined, true);
    }
    const rows = this.db.prepare(`
      SELECT * FROM arena_rooms
      WHERE status IN ('waiting', 'playing') AND updated_at >= ?
      ORDER BY updated_at DESC
      LIMIT ?
    `).all(cutoff, capped) as Array<Record<string, unknown>>;
    return rows.map((r) => this.rowToArenaRoom(r));
  }

  joinArenaRoom(code: string, userId: string, nowSec?: number): ArenaRoomRecord {
    const normCode = (code || "").trim().toUpperCase();
    if (!normCode) throw new Error("Room not found.");
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) throw new Error("Room not found.");
    const user = this.findUserById(userId);
    if (!user) throw new Error("User not found.");

    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) throw new Error("Room not found.");

    const persistSweepIfRejected = () => {
      if (sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
    };

    const existing = this.db.prepare(
      "SELECT user_id, username, dan_badge, connected FROM arena_players WHERE room_code = ? AND user_id = ?"
    ).get(normCode, user.id) as
      | { user_id: string; username: string; dan_badge: string; connected: number }
      | undefined;

    if (existing) {
      const wasDisconnected = Number(existing.connected ?? 1) === 0;
      const metaChanged =
        String(existing.username) !== user.username || String(existing.dan_badge || "") !== user.danBadge;
      this.db.prepare(
        "UPDATE arena_players SET username = ?, dan_badge = ?, connected = 1, last_seen_at = ? WHERE room_code = ? AND user_id = ?"
      ).run(user.username, user.danBadge, now, normCode, user.id);
      if (wasDisconnected || metaChanged || sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
      return this.readArenaRoomDirect(normCode)!;
    }

    const roomAfterSweep = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!roomAfterSweep) throw new Error("Room not found.");
    if (String(roomAfterSweep.status) === "playing") {
      persistSweepIfRejected();
      throw new Error("Match is already playing.");
    }

    const cnt = this.db.prepare(
      "SELECT COUNT(*) AS c FROM arena_players WHERE room_code = ?"
    ).get(normCode) as { c: number };
    if (Number(cnt.c || 0) >= 8) {
      persistSweepIfRejected();
      throw new Error("Room is full (maximum 8 players).");
    }

    this.db.prepare(`
      INSERT INTO arena_players (
        room_code, user_id, username, dan_badge, is_host, ready, finished,
        connected, last_seen_at, joined_at,
        score, accuracy, combo, max_combo, grade, updated_at
      ) VALUES (?, ?, ?, ?, 0, 0, 0, 1, ?, ?, 0, 0, 0, 0, 'D', ?)
    `).run(normCode, user.id, user.username, user.danBadge, now, now, now);
    this.db.prepare(
      "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
    ).run(now, normCode);

    return this.readArenaRoomDirect(normCode)!;
  }

  leaveArenaRoom(code: string, userId: string, nowSec?: number): ArenaRoomRecord | null {
    const normCode = (code || "").trim().toUpperCase();
    if (!normCode) return null;
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) return null;

    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) return null;

    const delRes = this.db.prepare("DELETE FROM arena_players WHERE room_code = ? AND user_id = ?").run(normCode, userId);
    this.chatCooldownMs.delete(`${normCode}:${userId}`);

    const remaining = this.db.prepare(`
      SELECT user_id, username, is_host, finished, connected, joined_at, rowid AS row_id
      FROM arena_players
      WHERE room_code = ?
      ORDER BY connected DESC, joined_at ASC, rowid ASC
    `).all(normCode) as Array<Record<string, unknown>>;

    if (remaining.length === 0) {
      this.db.prepare("DELETE FROM arena_rooms WHERE code = ?").run(normCode);
      return null;
    }

    const room = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as Record<string, unknown>;
    const hasHost = remaining.some((p) => Number(p.is_host) !== 0 && String(p.user_id) === String(room.host_id));
    let changed = Number(delRes.changes || 0) > 0 || sweep.changed;

    if (!hasHost) {
      const nextHost = remaining[0];
      const nextHostId = String(nextHost.user_id);
      const nextHostName = String(nextHost.username);
      this.db.prepare(
        "UPDATE arena_players SET is_host = CASE WHEN user_id = ? THEN 1 ELSE 0 END, ready = CASE WHEN user_id = ? THEN 1 ELSE ready END WHERE room_code = ?"
      ).run(nextHostId, nextHostId, normCode);
      this.db.prepare(
        "UPDATE arena_rooms SET host_id = ?, host_name = ? WHERE code = ?"
      ).run(nextHostId, nextHostName, normCode);
      changed = true;
    }

    if (String(room.status) === "playing" && remaining.every((p) => Number(p.finished) !== 0)) {
      this.db.prepare("UPDATE arena_rooms SET status = 'finished' WHERE code = ?").run(normCode);
      changed = true;
    }

    if (changed) {
      this.db.prepare(
        "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
      ).run(now, normCode);
    }

    return this.readArenaRoomDirect(normCode);
  }

  selectArenaChart(code: string, userId: string, chart: ArenaChartSelection, nowSec?: number): ArenaRoomRecord {
    const normCode = (code || "").trim().toUpperCase();
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) throw new Error("Room not found.");

    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) throw new Error("Room not found.");

    const persistSweepIfRejected = () => {
      if (sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
    };

    const room = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as Record<string, unknown>;
    if (String(room.host_id) !== userId) {
      persistSweepIfRejected();
      throw new Error("Only the room host can select the chart.");
    }
    const chartId = (chart.chartId || "").trim();
    const difficulty = (chart.difficulty || "").trim();
    if (!chartId || !difficulty) {
      persistSweepIfRejected();
      throw new Error("Missing chart_id or difficulty.");
    }
    const chartTitle = (chart.chartTitle || "").trim() || chartId;
    const difficultyLevel =
      Number.isFinite(chart.difficultyLevel) && chart.difficultyLevel > 0
        ? Number(chart.difficultyLevel)
        : 1.0;
    const chartHash = (chart.chartHash || "").trim();
    const linked = this.resolveMarketChartLink(chart.marketChartId, chartHash, chartId);

    this.db.prepare(`
      UPDATE arena_rooms SET
        status = 'waiting',
        revision = revision + 1,
        chart_id = ?,
        chart_title = ?,
        difficulty = ?,
        difficulty_level = ?,
        chart_hash = ?,
        market_chart_id = ?,
        package_size = ?,
        started_at = 0,
        updated_at = ?
      WHERE code = ?
    `).run(
      chartId,
      chartTitle,
      difficulty,
      difficultyLevel,
      chartHash,
      linked.marketChartId,
      linked.packageSize,
      now,
      normCode
    );

    this.db.prepare(`
      UPDATE arena_players SET
        finished = 0,
        score = 0,
        accuracy = 0,
        combo = 0,
        max_combo = 0,
        grade = 'D',
        connected = CASE WHEN user_id = ? THEN 1 ELSE connected END,
        last_seen_at = CASE WHEN user_id = ? THEN ? ELSE last_seen_at END,
        updated_at = ?
      WHERE room_code = ?
    `).run(userId, userId, now, now, normCode);

    return this.readArenaRoomDirect(normCode)!;
  }

  setArenaReady(
    code: string,
    userId: string,
    ready: boolean,
    startMatch = false,
    nowSec?: number
  ): ArenaRoomRecord {
    const normCode = (code || "").trim().toUpperCase();
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) throw new Error("Room not found.");

    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) throw new Error("Room not found.");

    const player = this.db.prepare(
      "SELECT user_id FROM arena_players WHERE room_code = ? AND user_id = ?"
    ).get(normCode, userId) as { user_id: string } | undefined;
    if (!player) {
      if (sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
      throw new Error("Player has not joined this room.");
    }

    const room = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as Record<string, unknown>;
    this.db.prepare(
      "UPDATE arena_players SET ready = ?, connected = 1, last_seen_at = ?, updated_at = ? WHERE room_code = ? AND user_id = ?"
    ).run(ready ? 1 : 0, now, now, normCode, userId);

    if (startMatch === true && String(room.host_id) === userId) {
      this.db.prepare(`
        UPDATE arena_players SET
          finished = 0,
          score = 0,
          accuracy = 0,
          combo = 0,
          max_combo = 0,
          grade = 'D',
          updated_at = ?
        WHERE room_code = ?
      `).run(now, normCode);
      this.db.prepare(
        "UPDATE arena_rooms SET status = 'playing', revision = revision + 1, started_at = ?, updated_at = ? WHERE code = ?"
      ).run(now, now, normCode);
    } else {
      this.db.prepare(
        "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
      ).run(now, normCode);
    }

    return this.readArenaRoomDirect(normCode)!;
  }

  reportArenaProgress(
    code: string,
    userId: string,
    progress: ArenaProgressInput,
    nowSec?: number
  ): ArenaRoomRecord {
    const normCode = (code || "").trim().toUpperCase();
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) throw new Error("Room not found.");

    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) throw new Error("Room not found.");

    const persistSweepIfRejected = () => {
      if (sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
    };

    const player = this.db.prepare(
      "SELECT user_id FROM arena_players WHERE room_code = ? AND user_id = ?"
    ).get(normCode, userId) as { user_id: string } | undefined;
    if (!player) {
      persistSweepIfRejected();
      throw new Error("Player has not joined this room.");
    }

    if (!Number.isFinite(progress.score) || progress.score < 0 || progress.score > 1000000) {
      persistSweepIfRejected();
      throw new Error("Score must be between 0 and 1,000,000.");
    }
    if (!Number.isFinite(progress.accuracy) || progress.accuracy < 0 || progress.accuracy > 100.01) {
      persistSweepIfRejected();
      throw new Error("Accuracy is out of valid range [0, 100].");
    }

    const cleanScore = Math.trunc(progress.score);
    const cleanAcc = Number(progress.accuracy);
    const cleanCombo = Math.max(0, Math.trunc(progress.combo || 0));
    const cleanMaxCombo = Math.max(cleanCombo, Math.max(0, Math.trunc(progress.maxCombo || 0)));
    const cleanGrade = (progress.grade || "D").trim() || "D";
    const cleanFinished = progress.finished ? 1 : 0;

    this.db.prepare(`
      UPDATE arena_players SET
        score = ?,
        accuracy = ?,
        combo = ?,
        max_combo = ?,
        grade = ?,
        finished = ?,
        connected = 1,
        last_seen_at = ?,
        updated_at = ?
      WHERE room_code = ? AND user_id = ?
    `).run(cleanScore, cleanAcc, cleanCombo, cleanMaxCombo, cleanGrade, cleanFinished, now, now, normCode, userId);

    const agg = this.db.prepare(
      "SELECT COUNT(*) AS total, COALESCE(SUM(CASE WHEN finished != 0 THEN 1 ELSE 0 END), 0) AS done FROM arena_players WHERE room_code = ?"
    ).get(normCode) as { total: number; done: number };

    if (Number(agg.total) > 0 && Number(agg.done) === Number(agg.total)) {
      this.db.prepare(
        "UPDATE arena_rooms SET status = 'finished', revision = revision + 1, updated_at = ? WHERE code = ?"
      ).run(now, normCode);
    } else {
      this.db.prepare(
        "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
      ).run(now, normCode);
    }

    return this.readArenaRoomDirect(normCode)!;
  }

  sendArenaChat(
    code: string,
    userId: string,
    phraseId: number,
    nowSec?: number,
    nowMs?: number
  ): ArenaRoomRecord {
    const normCode = (code || "").trim().toUpperCase();
    const initialRoom = this.db.prepare("SELECT * FROM arena_rooms WHERE code = ?").get(normCode) as
      | Record<string, unknown>
      | undefined;
    if (!initialRoom) throw new Error("Room not found.");

    const effectiveNowMs =
      nowMs !== undefined && Number.isFinite(nowMs)
        ? Math.trunc(nowMs)
        : nowSec !== undefined && Number.isFinite(nowSec)
          ? Math.floor(nowSec) * 1000
          : Date.now();
    const now =
      nowSec !== undefined && Number.isFinite(nowSec)
        ? Math.floor(nowSec)
        : Math.floor(effectiveNowMs / 1000);

    const sweep = this.sweepArenaRoomInternal(normCode, now, undefined, false);
    if (!sweep.exists) throw new Error("Room not found.");

    const persistSweepIfRejected = () => {
      if (sweep.changed) {
        this.db.prepare(
          "UPDATE arena_rooms SET revision = revision + 1, updated_at = ? WHERE code = ?"
        ).run(now, normCode);
      }
    };

    const player = this.db.prepare(
      "SELECT user_id, username FROM arena_players WHERE room_code = ? AND user_id = ?"
    ).get(normCode, userId) as { user_id: string; username: string } | undefined;
    if (!player) {
      persistSweepIfRejected();
      throw new Error("Player has not joined this room.");
    }

    if (!Number.isInteger(phraseId) || phraseId < 0 || phraseId >= ARENA_QUICK_CHAT_PHRASES.length) {
      persistSweepIfRejected();
      throw new Error("Invalid phrase_id; expected integer in 0..5.");
    }

    const cooldownKey = `${normCode}:${userId}`;
    const prevChatMs = this.chatCooldownMs.get(cooldownKey);
    if (prevChatMs !== undefined && effectiveNowMs - prevChatMs < 1500) {
      persistSweepIfRejected();
      throw new Error("Quick chat cooldown active; please wait 1.5s between messages.");
    }
    this.chatCooldownMs.set(cooldownKey, effectiveNowMs);

    const room = this.db.prepare("SELECT messages_json, next_msg_id FROM arena_rooms WHERE code = ?").get(normCode) as {
      messages_json: string;
      next_msg_id: number;
    };
    let messages: ArenaChatMessage[] = [];
    try {
      const parsed = JSON.parse(String(room.messages_json || "[]"));
      if (Array.isArray(parsed)) messages = parsed as ArenaChatMessage[];
    } catch {
      messages = [];
    }

    const msgId = Math.max(1, Number(room.next_msg_id || 1));
    const user = this.findUserById(userId);
    const senderName = user?.username || String(player.username);
    messages.push({
      id: msgId,
      userId,
      username: senderName,
      phraseId,
      text: ARENA_QUICK_CHAT_PHRASES[phraseId],
      sentAt: now,
    });
    if (messages.length > 16) {
      messages = messages.slice(messages.length - 16);
    }

    this.db.prepare(
      "UPDATE arena_players SET connected = 1, last_seen_at = ? WHERE room_code = ? AND user_id = ?"
    ).run(now, normCode, userId);
    this.db.prepare(
      "UPDATE arena_rooms SET messages_json = ?, next_msg_id = ?, revision = revision + 1, updated_at = ? WHERE code = ?"
    ).run(JSON.stringify(messages), msgId + 1, now, normCode);

    return this.readArenaRoomDirect(normCode)!;
  }

  getArenaRoomState(code: string, callerUserId?: string, nowSec?: number): ArenaRoomRecord | null {
    const normCode = (code || "").trim().toUpperCase();
    if (!normCode) return null;
    const now = this.resolveNowSec(nowSec);
    const sweep = this.sweepArenaRoomInternal(normCode, now, callerUserId, true);
    if (!sweep.exists) return null;
    return this.readArenaRoomDirect(normCode);
  }

  private sanitizeProfileText(raw: string | undefined, maxCodepoints: number): string {
    const cleaned = (raw ?? "").replace(/[\x00-\x1F\x7F]/g, "").trim();
    const cps = Array.from(cleaned);
    if (cps.length > maxCodepoints) {
      throw new Error(`Field exceeds maximum length of ${maxCodepoints} characters.`);
    }
    return cleaned;
  }

  private countUserPlays(userId: string): number {
    const r1 = this.db.prepare("SELECT COUNT(*) AS c FROM scores WHERE user_id = ?").get(userId) as { c: number };
    const r2 = this.db.prepare("SELECT COUNT(*) AS c FROM cloud_best_scores WHERE user_id = ?").get(userId) as { c: number };
    return Math.max(Number(r1.c || 0), Number(r2.c || 0));
  }

  getUserProfile(targetUserId: string, viewerUserId?: string): UserProfileData | null {
    const user = this.findUserById(targetUserId);
    if (!user) return null;

    const followerCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE target_id = ?").get(user.id) as { c: number }).c || 0
    );
    const rivalCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE follower_id = ?").get(user.id) as { c: number }).c || 0
    );
    let isRival = false;
    let isMutual = false;
    if (viewerUserId && viewerUserId !== user.id) {
      isRival = Boolean(
        this.db.prepare("SELECT 1 FROM user_follows WHERE follower_id = ? AND target_id = ?").get(viewerUserId, user.id)
      );
      const followsBack = Boolean(
        this.db.prepare("SELECT 1 FROM user_follows WHERE follower_id = ? AND target_id = ?").get(user.id, viewerUserId)
      );
      isMutual = isRival && followsBack;
    }

    const bestMap = new Map<string, UserProfileScoreItem>();
    const scoreRows = this.db.prepare(`
      SELECT id, chart_id, chart_title, difficulty, difficulty_level, score, accuracy, max_combo,
             grade, pp, synergy_count, is_full_combo, is_all_perfect, has_replay, played_at
      FROM scores
      WHERE user_id = ?
      ORDER BY score DESC, accuracy DESC, played_at ASC
    `).all(user.id) as Array<Record<string, unknown>>;

    for (const r of scoreRows) {
      const key = `${String(r.chart_id)}::${String(r.difficulty)}`;
      const fc = Number(r.is_full_combo) !== 0;
      const ap = Number(r.is_all_perfect) !== 0;
      const cr = Math.round(
        computeChartRating(Number(r.difficulty_level), Number(r.accuracy), fc, ap, Number(r.synergy_count || 0)) * 100
      ) / 100;
      const prev = bestMap.get(key);
      if (!prev || cr > prev.chartRating || (cr === prev.chartRating && Number(r.score) > prev.score)) {
        bestMap.set(key, {
          scoreId: String(r.id),
          chartId: String(r.chart_id),
          chartTitle: String(r.chart_title),
          difficulty: String(r.difficulty),
          difficultyLevel: Number(r.difficulty_level),
          score: Number(r.score),
          accuracy: Number(r.accuracy),
          maxCombo: Number(r.max_combo),
          grade: String(r.grade),
          pp: Number(r.pp),
          chartRating: cr,
          isFullCombo: fc,
          isAllPerfect: ap,
          hasReplay: Number(r.has_replay) !== 0,
          playedAt: Number(r.played_at),
        });
      }
    }

    const bestScores = Array.from(bestMap.values())
      .sort((a, b) => b.chartRating - a.chartRating || b.pp - a.pp || b.score - a.score)
      .slice(0, 15);

    return {
      userId: user.id,
      username: user.username,
      isGuest: user.isGuest,
      bio: user.bio ?? "",
      featuredTitle: user.featuredTitle ?? "",
      danBadge: user.danBadge,
      sakuraRating: this.getUserSakuraRating(user.id),
      totalPP: this.getUserTotalPP(user.id),
      playCount: this.countUserPlays(user.id),
      followerCount,
      rivalCount,
      isRival,
      isMutual,
      createdAt: user.createdAt,
      bestScores,
    };
  }

  updateUserProfile(userId: string, input: { bio?: string; featuredTitle?: string }): UserProfileData {
    const existing = this.findUserById(userId);
    if (!existing) throw new Error("User not found.");
    const nextBio =
      input.bio !== undefined ? this.sanitizeProfileText(input.bio, 120) : (existing.bio ?? "");
    const nextTitle =
      input.featuredTitle !== undefined
        ? this.sanitizeProfileText(input.featuredTitle, 32)
        : (existing.featuredTitle ?? "");
    this.db.prepare("UPDATE users SET bio = ?, featured_title = ? WHERE id = ?").run(nextBio, nextTitle, userId);
    return this.getUserProfile(userId, userId)!;
  }

  addRival(
    userId: string,
    targetUserId: string
  ): { isRival: boolean; isMutual: boolean; rivalCount: number; followerCount: number } {
    if (!userId || !targetUserId || userId === targetUserId) {
      throw new Error("Cannot add yourself as a rival.");
    }
    const target = this.findUserById(targetUserId);
    if (!target) {
      throw new Error("Target user not found.");
    }
    const already = Boolean(
      this.db.prepare("SELECT 1 FROM user_follows WHERE follower_id = ? AND target_id = ?").get(userId, targetUserId)
    );
    if (!already) {
      const curCnt = Number(
        (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE follower_id = ?").get(userId) as { c: number }).c || 0
      );
      if (curCnt >= 100) {
        throw new Error("Maximum limit of 100 rivals reached.");
      }
      const now = Math.floor(Date.now() / 1000);
      this.db.prepare("INSERT OR IGNORE INTO user_follows (follower_id, target_id, created_at) VALUES (?, ?, ?)").run(
        userId,
        targetUserId,
        now
      );
    }
    const isMutual = Boolean(
      this.db.prepare("SELECT 1 FROM user_follows WHERE follower_id = ? AND target_id = ?").get(targetUserId, userId)
    );
    const rivalCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE follower_id = ?").get(userId) as { c: number }).c || 0
    );
    const followerCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE target_id = ?").get(targetUserId) as { c: number }).c || 0
    );
    return { isRival: true, isMutual, rivalCount, followerCount };
  }

  removeRival(
    userId: string,
    targetUserId: string
  ): { isRival: boolean; rivalCount: number; followerCount: number } {
    this.db.prepare("DELETE FROM user_follows WHERE follower_id = ? AND target_id = ?").run(userId, targetUserId);
    const rivalCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE follower_id = ?").get(userId) as { c: number }).c || 0
    );
    const followerCount = Number(
      (this.db.prepare("SELECT COUNT(*) AS c FROM user_follows WHERE target_id = ?").get(targetUserId) as { c: number }).c || 0
    );
    return { isRival: false, rivalCount, followerCount };
  }

  listRivals(userId: string): RivalSummaryItem[] {
    const rows = this.db.prepare(`
      SELECT f.target_id, f.created_at AS added_at, u.username, u.bio, u.featured_title, u.dan_badge
      FROM user_follows f
      JOIN users u ON u.id = f.target_id
      WHERE f.follower_id = ?
    `).all(userId) as Array<Record<string, unknown>>;

    const items: RivalSummaryItem[] = rows.map((r) => {
      const tid = String(r.target_id);
      const isMutual = Boolean(
        this.db.prepare("SELECT 1 FROM user_follows WHERE follower_id = ? AND target_id = ?").get(tid, userId)
      );
      return {
        userId: tid,
        username: String(r.username),
        bio: String(r.bio || ""),
        featuredTitle: String(r.featured_title || ""),
        danBadge: String(r.dan_badge || ""),
        sakuraRating: this.getUserSakuraRating(tid),
        totalPP: this.getUserTotalPP(tid),
        playCount: this.countUserPlays(tid),
        isMutual,
        addedAt: Number(r.added_at || 0),
      };
    });

    items.sort((a, b) => b.sakuraRating - a.sakuraRating || b.totalPP - a.totalPP || b.addedAt - a.addedAt);
    return items;
  }

  getGlobalRatingLadder(
    limit = 100,
    viewerUserId?: string
  ): { entries: RatingLadderEntry[]; myRank: number | null; myRating: number } {
    const capped = Math.max(1, Math.min(100, Math.trunc(limit || 100)));
    const viewerRivals = new Set<string>();
    if (viewerUserId) {
      const rRows = this.db.prepare("SELECT target_id FROM user_follows WHERE follower_id = ?").all(viewerUserId) as Array<{
        target_id: string;
      }>;
      for (const rr of rRows) viewerRivals.add(String(rr.target_id));
    }

    const users = this.db.prepare(
      "SELECT id, username, is_guest, featured_title, dan_badge FROM users"
    ).all() as Array<Record<string, unknown>>;

    const ranked: Array<Omit<RatingLadderEntry, "rank">> = [];
    for (const u of users) {
      const uid = String(u.id);
      const playCount = this.countUserPlays(uid);
      const isGuest = Number(u.is_guest) !== 0;
      if (isGuest && playCount <= 0) continue;
      const sakuraRating = this.getUserSakuraRating(uid);
      const totalPP = this.getUserTotalPP(uid);
      if (playCount <= 0 && sakuraRating <= 0 && totalPP <= 0) continue;
      ranked.push({
        userId: uid,
        username: String(u.username),
        featuredTitle: String(u.featured_title || ""),
        danBadge: String(u.dan_badge || ""),
        sakuraRating,
        totalPP,
        playCount,
        isRival: viewerRivals.has(uid),
      });
    }

    ranked.sort((a, b) => b.sakuraRating - a.sakuraRating || b.totalPP - a.totalPP || b.playCount - a.playCount);
    let myRank: number | null = null;
    const myRating = viewerUserId ? this.getUserSakuraRating(viewerUserId) : 0;
    const entries: RatingLadderEntry[] = [];
    for (let i = 0; i < ranked.length; i++) {
      const rank = i + 1;
      if (viewerUserId && ranked[i].userId === viewerUserId) {
        myRank = rank;
      }
      if (entries.length < capped) {
        entries.push({ rank, ...ranked[i] });
      }
    }
    return { entries, myRank, myRating };
  }

  private aggregateSeasonStandings(seasonId: string) {
    const rows = this.db.prepare(`
      SELECT s.user_id, s.slot_index, s.score, s.accuracy, s.stage_sp, s.updated_at,
             u.username, u.featured_title, u.dan_badge
      FROM season_scores s
      JOIN users u ON u.id = s.user_id
      WHERE s.season_id = ?
    `).all(seasonId) as Array<Record<string, unknown>>;

    const map = new Map<string, {
      userId: string;
      username: string;
      featuredTitle: string;
      danBadge: string;
      stageScores: [number, number, number];
      stageAccuracies: [number, number, number];
      stageSP: [number, number, number];
      totalSP: number;
      totalScore: number;
      maxUpdatedAt: number;
    }>();

    for (const r of rows) {
      const uid = String(r.user_id);
      const slot = Number(r.slot_index);
      if (slot < 0 || slot > 2) continue;
      let rec = map.get(uid);
      if (!rec) {
        rec = {
          userId: uid,
          username: String(r.username),
          featuredTitle: String(r.featured_title || ""),
          danBadge: String(r.dan_badge || ""),
          stageScores: [0, 0, 0],
          stageAccuracies: [0, 0, 0],
          stageSP: [0, 0, 0],
          totalSP: 0,
          totalScore: 0,
          maxUpdatedAt: 0,
        };
        map.set(uid, rec);
      }
      rec.stageScores[slot] = Number(r.score || 0);
      rec.stageAccuracies[slot] = Number(r.accuracy || 0);
      rec.stageSP[slot] = Number(r.stage_sp || 0);
      rec.maxUpdatedAt = Math.max(rec.maxUpdatedAt, Number(r.updated_at || 0));
    }

    const list = Array.from(map.values());
    for (const item of list) {
      item.totalSP = item.stageSP[0] + item.stageSP[1] + item.stageSP[2];
      item.totalScore = item.stageScores[0] + item.stageScores[1] + item.stageScores[2];
    }
    list.sort((a, b) => b.totalSP - a.totalSP || b.totalScore - a.totalScore || a.maxUpdatedAt - b.maxUpdatedAt);
    return list;
  }

  getCurrentSeason(seasonIdUtc8?: string, viewerUserId?: string): SeasonCurrentPayload {
    const spec = computeSeasonSpec(seasonIdUtc8);
    let myStanding: SeasonMyStanding | null = null;
    if (viewerUserId && this.findUserById(viewerUserId)) {
      const standings = this.aggregateSeasonStandings(spec.seasonId);
      const idx = standings.findIndex((s) => s.userId === viewerUserId);
      if (idx >= 0) {
        const me = standings[idx];
        const tier = evaluateSeasonTier(me.totalSP);
        myStanding = {
          stageScores: me.stageScores,
          stageAccuracies: me.stageAccuracies,
          stageSP: me.stageSP,
          totalSP: me.totalSP,
          rankTier: tier.rankTier,
          tierIndex: tier.tierIndex,
          nextTierName: tier.nextTierName,
          nextTierThreshold: tier.nextTierThreshold,
          spToNextTier: tier.spToNextTier,
          rank: idx + 1,
        };
      } else {
        const tier = evaluateSeasonTier(0);
        myStanding = {
          stageScores: [0, 0, 0],
          stageAccuracies: [0, 0, 0],
          stageSP: [0, 0, 0],
          totalSP: 0,
          rankTier: tier.rankTier,
          tierIndex: tier.tierIndex,
          nextTierName: tier.nextTierName,
          nextTierThreshold: tier.nextTierThreshold,
          spToNextTier: tier.spToNextTier,
          rank: null,
        };
      }
    }
    return { seasonId: spec.seasonId, themeTitle: spec.themeTitle, stages: spec.stages, myStanding };
  }

  submitSeasonScore(userId: string, sub: ScoreSubmission, seasonIdUtc8?: string) {
    const user = this.findUserById(userId);
    if (!user) throw new Error("User not found.");
    if (sub.assisted) throw new Error("Assisted plays cannot be submitted to Season Ranked.");
    const rate = sub.playbackRate !== undefined ? Number(sub.playbackRate) : 1.0;
    if (!Number.isFinite(rate) || Math.abs(rate - 1.0) >= 0.01) {
      throw new Error("Only 1.0x standard speed plays are eligible for Season Ranked.");
    }
    if (!Number.isFinite(sub.score) || sub.score <= 0 || sub.score > 1_000_000) {
      throw new Error("Invalid score value for Season Ranked.");
    }
    if (!Number.isFinite(sub.accuracy) || sub.accuracy <= 0 || sub.accuracy > 100.01) {
      throw new Error("Invalid accuracy value for Season Ranked.");
    }

    const spec = computeSeasonSpec(seasonIdUtc8);
    const slotIndex = spec.stages.findIndex(
      (st) => st.chartId === (sub.chartId || "").trim() && st.difficulty === (sub.difficulty || "").trim()
    );
    if (slotIndex < 0) {
      throw new Error("Submitted chart and difficulty do not match any active Season Ranked stage.");
    }
    const stage = spec.stages[slotIndex];
    const stageSP = calculateStageSP(sub, stage.difficultyLevel, slotIndex);
    const now = Math.floor(Date.now() / 1000);

    const prev = this.db.prepare(
      "SELECT stage_sp, score FROM season_scores WHERE season_id = ? AND user_id = ? AND slot_index = ?"
    ).get(spec.seasonId, user.id, slotIndex) as { stage_sp: number; score: number } | undefined;

    const newBest =
      !prev ||
      stageSP > Number(prev.stage_sp) ||
      (stageSP === Number(prev.stage_sp) && Math.trunc(sub.score) > Number(prev.score));

    if (newBest) {
      this.db.prepare(`
        INSERT INTO season_scores (
          season_id, user_id, slot_index, chart_id, difficulty, difficulty_level,
          score, accuracy, max_combo, grade, is_full_combo, is_all_perfect,
          stage_sp, replay_data, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(season_id, user_id, slot_index) DO UPDATE SET
          score = excluded.score,
          accuracy = excluded.accuracy,
          max_combo = excluded.max_combo,
          grade = excluded.grade,
          is_full_combo = excluded.is_full_combo,
          is_all_perfect = excluded.is_all_perfect,
          stage_sp = excluded.stage_sp,
          replay_data = COALESCE(excluded.replay_data, season_scores.replay_data),
          updated_at = excluded.updated_at
      `).run(
        spec.seasonId, user.id, slotIndex, stage.chartId, stage.difficulty, stage.difficultyLevel,
        Math.trunc(sub.score), Number(sub.accuracy), Math.max(0, Math.trunc(sub.maxCombo || 0)),
        (sub.grade || "D").trim() || "D", sub.isFullCombo ? 1 : 0, sub.isAllPerfect ? 1 : 0,
        stageSP, sub.replayData ?? null, now
      );
    }

    const standings = this.aggregateSeasonStandings(spec.seasonId);
    const myIdx = standings.findIndex((s) => s.userId === user.id);
    const totalSP = myIdx >= 0 ? standings[myIdx].totalSP : stageSP;
    const rank = myIdx >= 0 ? myIdx + 1 : 1;
    const tier = evaluateSeasonTier(totalSP);
    return {
      seasonId: spec.seasonId,
      slotIndex,
      stageSP,
      totalSP,
      rankTier: tier.rankTier,
      tierIndex: tier.tierIndex,
      rank,
      newBest,
    };
  }

  getSeasonLeaderboard(seasonIdUtc8?: string, limit = 100, viewerUserId?: string) {
    const spec = computeSeasonSpec(seasonIdUtc8);
    const capped = Math.max(1, Math.min(100, Math.trunc(limit || 100)));
    const standings = this.aggregateSeasonStandings(spec.seasonId);
    let myRank: number | null = null;
    const entries: SeasonLeaderboardEntry[] = [];
    for (let i = 0; i < standings.length; i++) {
      const rank = i + 1;
      if (viewerUserId && standings[i].userId === viewerUserId) myRank = rank;
      if (entries.length < capped) {
        const s = standings[i];
        const tier = evaluateSeasonTier(s.totalSP);
        entries.push({
          rank,
          userId: s.userId,
          username: s.username,
          featuredTitle: s.featuredTitle,
          danBadge: s.danBadge,
          totalSP: s.totalSP,
          rankTier: tier.rankTier,
          stageSP: s.stageSP,
        });
      }
    }
    return { seasonId: spec.seasonId, entries, myRank };
  }

  close(): void {
    this.db.close();
  }
}
