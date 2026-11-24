export interface UserRecord {
  id: string;
  username: string;
  passwordHash: string;
  isGuest: boolean;
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
  score: number;
  accuracy: number;
  maxCombo: number;
  grade: string;
  pp: number;
  isFullCombo: boolean;
  isAllPerfect: boolean;
  hasReplay: boolean;
  playedAt: number;
}

export interface IStorageAdapter {
  readonly mode: "sqlite" | "postgres";
  createGuestUser(nickname?: string): UserRecord;
  registerUser(username: string, passwordHash: string): UserRecord;
  findUserByUsername(username: string): UserRecord | null;
  findUserById(id: string): UserRecord | null;
  getUserTotalPP(userId: string): number;
  submitScore(userId: string, sub: ScoreSubmission): { scoreId: string; pp: number; rank: number; newBest: boolean };
  getLeaderboard(chartId: string, difficulty: string, limit: number, currentUserId?: string): {
    entries: LeaderboardEntry[];
    myRank: number | null;
  };
  getReplayData(scoreId: string): string | null;
  close(): void;
}