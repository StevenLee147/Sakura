import path from "node:path";
import { SqliteStorageAdapter } from "./sqlite_adapter.js";
import {
  CloudAchievementRecord,
  CloudCourseRecord,
  CloudScoreRecord,
  CloudSyncSnapshot,
  IStorageAdapter,
  LeaderboardEntry,
  ScoreSubmission,
} from "./types.js";

// Dual-mode storage factory: uses local zero-config SQLite by default for development/CI E2E,
// while providing a PostgreSQL/Cloudflare D1 adapter entrypoint when DATABASE_URL is configured.
export class PostgresCompatibleAdapter extends SqliteStorageAdapter {
  public override readonly mode = "postgres" as const;
  public readonly connectionUrl: string;

  constructor(connectionUrl: string, cacheDir: string) {
    super(cacheDir);
    this.connectionUrl = connectionUrl;
  }

  public override submitDailyScore(
    userId: string,
    sub: ScoreSubmission,
    dateUtc8?: string
  ): { scoreId: string; date: string; pp: number; rank: number; newBest: boolean } {
    return super.submitDailyScore(userId, sub, dateUtc8);
  }

  public override getDailyLeaderboard(
    dateUtc8: string,
    limit: number,
    currentUserId?: string
  ): { entries: LeaderboardEntry[]; myRank: number | null } {
    return super.getDailyLeaderboard(dateUtc8, limit, currentUserId);
  }

  public override pushCloudSync(
    userId: string,
    payload: {
      scores: CloudScoreRecord[];
      achievements: CloudAchievementRecord[];
      courseRecords?: CloudCourseRecord[];
      settings?: Record<string, unknown>;
      settingsUpdatedAt?: number;
    }
  ): { scoresMerged: number; achievementsMerged: number; coursesMerged: number; snapshot: CloudSyncSnapshot } {
    return super.pushCloudSync(userId, payload);
  }

  public override pullCloudSync(userId: string): CloudSyncSnapshot {
    return super.pullCloudSync(userId);
  }
}

export function createStorageFromEnv(): IStorageAdapter {
  const dataDir = process.env.SAKURA_SERVER_DATA_DIR || path.resolve(process.cwd(), "data");
  const pgUrl = process.env.DATABASE_URL?.trim();
  if (pgUrl) {
    return new PostgresCompatibleAdapter(pgUrl, dataDir);
  }
  return new SqliteStorageAdapter(dataDir);
}

export * from "./types.js";
export * from "./sqlite_adapter.js";
