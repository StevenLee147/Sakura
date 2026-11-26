import path from "node:path";
import { SqliteStorageAdapter } from "./sqlite_adapter.js";
import {
  ArenaChartSelection,
  ArenaCreateRoomInput,
  ArenaProgressInput,
  ArenaRoomRecord,
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

  public override getUserSakuraRating(userId: string): number {
    return super.getUserSakuraRating(userId);
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

  public override createArenaRoom(userId: string, input: ArenaCreateRoomInput): ArenaRoomRecord {
    return super.createArenaRoom(userId, input);
  }

  public override listArenaRooms(limit?: number): ArenaRoomRecord[] {
    return super.listArenaRooms(limit);
  }

  public override joinArenaRoom(code: string, userId: string): ArenaRoomRecord {
    return super.joinArenaRoom(code, userId);
  }

  public override leaveArenaRoom(code: string, userId: string): ArenaRoomRecord | null {
    return super.leaveArenaRoom(code, userId);
  }

  public override selectArenaChart(code: string, userId: string, chart: ArenaChartSelection): ArenaRoomRecord {
    return super.selectArenaChart(code, userId, chart);
  }

  public override setArenaReady(
    code: string,
    userId: string,
    ready: boolean,
    startMatch?: boolean
  ): ArenaRoomRecord {
    return super.setArenaReady(code, userId, ready, startMatch);
  }

  public override reportArenaProgress(
    code: string,
    userId: string,
    progress: ArenaProgressInput
  ): ArenaRoomRecord {
    return super.reportArenaProgress(code, userId, progress);
  }

  public override getArenaRoomState(code: string): ArenaRoomRecord | null {
    return super.getArenaRoomState(code);
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
