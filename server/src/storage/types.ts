export interface UserRecord {
  id: string;
  username: string;
  passwordHash: string;
  isGuest: boolean;
  danBadge: string;
  createdAt: number;
}

export interface ScoreSubmission {
  chartId: string;
  chartTitle: string;
  chartHash: string;
  difficulty: string;
  difficultyLevel: number;
  score: number;
  accuracy: number;
  maxCombo: number;
  grade: string;
  perfectCount: number;
  greatCount: number;
  goodCount: number;
  badCount: number;
  missCount: number;
  totalJudgments: number;
  synergyCount: number;
  isFullCombo: boolean;
  isAllPerfect: boolean;
  assisted: boolean;
  playbackRate: number;
  replayData?: string;
}

export interface LeaderboardEntry {
  rank: number;
  scoreId: string;
  userId: string;
  username: string;
  danBadge: string;
  score: number;
  accuracy: number;
  maxCombo: number;
  grade: string;
  pp: number;
  sakuraRating: number;
  isFullCombo: boolean;
  isAllPerfect: boolean;
  hasReplay: boolean;
  playedAt: number;
}

export interface DailyChallengeSpec {
  date: string;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  goalText: string;
  targetScore: number;
}

export interface MarketDifficultyInfo {
  name: string;
  level: number;
  chartFile: string;
  noteCount: number;
}

export interface MarketChartItem {
  id: string;
  chartSlug: string;
  uploaderId: string;
  uploaderName: string;
  title: string;
  artist: string;
  charter: string;
  description: string;
  bpm: number;
  minLevel: number;
  maxLevel: number;
  tags: string[];
  difficulties: MarketDifficultyInfo[];
  packageSha256: string;
  contentHash: string;
  packageSize: number;
  downloads: number;
  ratingAvg: number;
  ratingCount: number;
  myRating?: number | null;
  createdAt: number;
  updatedAt: number;
}

export interface MarketSearchQuery {
  q?: string;
  minLevel?: number;
  maxLevel?: number;
  tag?: string;
  sort?: "popular" | "newest" | "rating";
  page?: number;
  pageSize?: number;
  currentUserId?: string;
}

export interface MarketRatingEntry {
  userId: string;
  username: string;
  rating: number;
  comment: string;
  updatedAt: number;
}

export interface CloudScoreRecord {
  chartId: string;
  chartTitle: string;
  chartHash: string;
  difficulty: string;
  difficultyLevel: number;
  score: number;
  accuracy: number;
  maxCombo: number;
  grade: string;
  perfectCount: number;
  greatCount: number;
  goodCount: number;
  badCount: number;
  missCount: number;
  isFullCombo: boolean;
  isAllPerfect: boolean;
  playedAt: number;
}

export interface CloudAchievementRecord {
  id: string;
  unlockedAt: number;
}

export interface CloudCourseRecord {
  courseId: string;
  badgeText: string;
  danRank: number;
  totalScore: number;
  avgAccuracy: number;
  remainingLife: number;
  cleared: boolean;
  clearedAt: number;
}

export interface CloudSyncSnapshot {
  syncedAt: number;
  scores: CloudScoreRecord[];
  achievements: CloudAchievementRecord[];
  courseRecords: CloudCourseRecord[];
  settings: Record<string, unknown>;
  settingsUpdatedAt: number;
}

export type ArenaRoomStatus = "waiting" | "playing" | "finished";

export interface ArenaPlayerRecord {
  userId: string;
  username: string;
  danBadge: string;
  isHost: boolean;
  ready: boolean;
  finished: boolean;
  score: number;
  accuracy: number;
  combo: number;
  maxCombo: number;
  grade: string;
  placement: number;
}

export interface ArenaRoomRecord {
  code: string;
  name: string;
  hostId: string;
  hostName: string;
  status: ArenaRoomStatus;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash: string;
  startedAt: number;
  updatedAt: number;
  players: ArenaPlayerRecord[];
}

export interface ArenaCreateRoomInput {
  name: string;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash?: string;
}

export interface ArenaChartSelection {
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash?: string;
}

export interface ArenaProgressInput {
  score: number;
  accuracy: number;
  combo: number;
  maxCombo: number;
  grade: string;
  finished: boolean;
}

export interface IStorageAdapter {
  readonly mode: "sqlite" | "postgres";
  createGuestUser(nickname?: string): UserRecord;
  registerUser(username: string, passwordHash: string): UserRecord;
  findUserByUsername(username: string): UserRecord | null;
  findUserById(id: string): UserRecord | null;
  getUserTotalPP(userId: string): number;
  getUserSakuraRating(userId: string): number;
  submitScore(userId: string, sub: ScoreSubmission): { scoreId: string; pp: number; rank: number; newBest: boolean };
  getLeaderboard(chartId: string, difficulty: string, limit: number, currentUserId?: string): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  };
  submitDailyScore(userId: string, sub: ScoreSubmission, dateUtc8?: string): {
    scoreId: string;
    date: string;
    pp: number;
    rank: number;
    newBest: boolean;
  };
  getDailyLeaderboard(dateUtc8: string, limit: number, currentUserId?: string): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  };
  getReplayData(scoreId: string): string | null;
  publishChart(userId: string, username: string, pkgBuffer: Buffer, description?: string): MarketChartItem;
  listCharts(query: MarketSearchQuery): { total: number; page: number; pageSize: number; charts: MarketChartItem[] };
  getChartById(chartId: string, currentUserId?: string): { chart: MarketChartItem; ratings: MarketRatingEntry[] } | null;
  downloadChartPackage(chartId: string): { buffer: Buffer; chart: MarketChartItem } | null;
  rateChart(chartId: string, userId: string, username: string, rating: number, comment: string): { ratingAvg: number; ratingCount: number; myRating: number };
  pushCloudSync(
    userId: string,
    payload: {
      scores: CloudScoreRecord[];
      achievements: CloudAchievementRecord[];
      courseRecords?: CloudCourseRecord[];
      settings?: Record<string, unknown>;
      settingsUpdatedAt?: number;
    }
  ): { scoresMerged: number; achievementsMerged: number; coursesMerged: number; snapshot: CloudSyncSnapshot };
  pullCloudSync(userId: string): CloudSyncSnapshot;
  createArenaRoom(userId: string, input: ArenaCreateRoomInput): ArenaRoomRecord;
  listArenaRooms(limit?: number): ArenaRoomRecord[];
  joinArenaRoom(code: string, userId: string): ArenaRoomRecord;
  leaveArenaRoom(code: string, userId: string): ArenaRoomRecord | null;
  selectArenaChart(code: string, userId: string, chart: ArenaChartSelection): ArenaRoomRecord;
  setArenaReady(code: string, userId: string, ready: boolean, startMatch?: boolean): ArenaRoomRecord;
  reportArenaProgress(code: string, userId: string, progress: ArenaProgressInput): ArenaRoomRecord;
  getArenaRoomState(code: string): ArenaRoomRecord | null;
  close(): void;
}