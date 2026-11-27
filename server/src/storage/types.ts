export interface UserRecord {
  id: string;
  username: string;
  passwordHash: string;
  isGuest: boolean;
  danBadge: string;
  bio?: string;
  featuredTitle?: string;
  createdAt: number;
}

export interface UserProfileScoreItem {
  scoreId: string;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  score: number;
  accuracy: number;
  maxCombo: number;
  grade: string;
  pp: number;
  chartRating: number;
  isFullCombo: boolean;
  isAllPerfect: boolean;
  hasReplay: boolean;
  playedAt: number;
}

export interface UserProfileData {
  userId: string;
  username: string;
  isGuest: boolean;
  bio: string;
  featuredTitle: string;
  danBadge: string;
  sakuraRating: number;
  totalPP: number;
  playCount: number;
  followerCount: number;
  rivalCount: number;
  isRival: boolean;
  isMutual: boolean;
  createdAt: number;
  bestScores: UserProfileScoreItem[];
}

export interface RivalSummaryItem {
  userId: string;
  username: string;
  bio: string;
  featuredTitle: string;
  danBadge: string;
  sakuraRating: number;
  totalPP: number;
  playCount: number;
  isMutual: boolean;
  addedAt: number;
}

export interface RatingLadderEntry {
  rank: number;
  userId: string;
  username: string;
  featuredTitle: string;
  danBadge: string;
  sakuraRating: number;
  totalPP: number;
  playCount: number;
  isRival: boolean;
}

export interface AuthSessionRecord {
  id: string;
  userId: string;
  refreshTokenHash: string;
  deviceName: string;
  createdAt: number;
  expiresAt: number;
  revokedAt: number | null;
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
  connected: boolean;
  lastSeenAt: number;
  score: number;
  accuracy: number;
  combo: number;
  maxCombo: number;
  grade: string;
  placement: number;
}

export interface ArenaChatMessage {
  id: number;
  userId: string;
  username: string;
  phraseId: number;
  text: string;
  sentAt: number;
}

export interface ArenaRoomRecord {
  code: string;
  name: string;
  hostId: string;
  hostName: string;
  status: ArenaRoomStatus;
  revision: number;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash: string;
  marketChartId: string;
  packageSize: number;
  startedAt: number;
  updatedAt: number;
  players: ArenaPlayerRecord[];
  messages: ArenaChatMessage[];
}

export interface ArenaCreateRoomInput {
  name: string;
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash?: string;
  marketChartId?: string;
}

export interface ArenaChartSelection {
  chartId: string;
  chartTitle: string;
  difficulty: string;
  difficultyLevel: number;
  chartHash?: string;
  marketChartId?: string;
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
  createAuthSession(
    userId: string,
    refreshTokenHash: string,
    expiresAt: number,
    deviceName?: string
  ): AuthSessionRecord;
  rotateAuthSession(
    oldRefreshTokenHash: string,
    newRefreshTokenHash: string,
    newExpiresAt: number,
    deviceName?: string
  ): UserRecord | null;
  revokeAuthSession(refreshTokenHash: string): boolean;
  revokeUserAuthSessions(userId: string, exceptRefreshTokenHash?: string): number;
  upgradeGuestUser(userId: string, username: string, passwordHash: string): UserRecord;
  updateUserPassword(userId: string, passwordHash: string): boolean;
  getUserTotalPP(userId: string): number;
  getUserSakuraRating(userId: string): number;
  submitScore(userId: string, sub: ScoreSubmission): { scoreId: string; pp: number; rank: number; newBest: boolean };
  getLeaderboard(
    chartId: string,
    difficulty: string,
    limit: number,
    currentUserId?: string,
    scope?: "global" | "rivals"
  ): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  };
  getUserProfile(targetUserId: string, viewerUserId?: string): UserProfileData | null;
  updateUserProfile(userId: string, input: { bio?: string; featuredTitle?: string }): UserProfileData;
  addRival(
    userId: string,
    targetUserId: string
  ): { isRival: boolean; isMutual: boolean; rivalCount: number; followerCount: number };
  removeRival(
    userId: string,
    targetUserId: string
  ): { isRival: boolean; rivalCount: number; followerCount: number };
  listRivals(userId: string): RivalSummaryItem[];
  getGlobalRatingLadder(
    limit?: number,
    viewerUserId?: string
  ): { entries: RatingLadderEntry[]; myRank: number | null; myRating: number };
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
  createArenaRoom(userId: string, input: ArenaCreateRoomInput, nowSec?: number): ArenaRoomRecord;
  listArenaRooms(limit?: number, nowSec?: number): ArenaRoomRecord[];
  joinArenaRoom(code: string, userId: string, nowSec?: number): ArenaRoomRecord;
  leaveArenaRoom(code: string, userId: string, nowSec?: number): ArenaRoomRecord | null;
  selectArenaChart(code: string, userId: string, chart: ArenaChartSelection, nowSec?: number): ArenaRoomRecord;
  setArenaReady(code: string, userId: string, ready: boolean, startMatch?: boolean, nowSec?: number): ArenaRoomRecord;
  reportArenaProgress(code: string, userId: string, progress: ArenaProgressInput, nowSec?: number): ArenaRoomRecord;
  sendArenaChat(code: string, userId: string, phraseId: number, nowSec?: number, nowMs?: number): ArenaRoomRecord;
  getArenaRoomState(code: string, callerUserId?: string, nowSec?: number): ArenaRoomRecord | null;
  sweepArenaHeartbeats(nowSec?: number): number;
  close(): void;
}