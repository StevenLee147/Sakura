import crypto from "node:crypto";
import { Context, Hono } from "hono";
import { cors } from "hono/cors";
import {
  generateRefreshToken,
  hashPassword,
  hashRefreshToken,
  JwtPayload,
  REFRESH_TOKEN_TTL_SEC,
  signJwtWithExpiry,
  verifyJwt,
  verifyPassword,
} from "./auth.js";
import { computeChartRating, computeDailyChallenge } from "./storage/sqlite_adapter.js";
import {
  ArenaRoomRecord,
  CloudAchievementRecord,
  CloudCourseRecord,
  CloudScoreRecord,
  IStorageAdapter,
  LeaderboardEntry,
  MarketChartItem,
  ScoreSubmission,
  UserRecord,
} from "./storage/types.js";

export { computeChartRating, computeDailyChallenge };

function serializeLeaderboardEntry(e: LeaderboardEntry) {
  return {
    rank: e.rank,
    score_id: e.scoreId,
    user_id: e.userId,
    username: e.username,
    dan_badge: e.danBadge,
    score: e.score,
    accuracy: e.accuracy,
    max_combo: e.maxCombo,
    grade: e.grade,
    pp: e.pp,
    sakura_rating: e.sakuraRating,
    is_full_combo: e.isFullCombo,
    is_all_perfect: e.isAllPerfect,
    has_replay: e.hasReplay,
    played_at: e.playedAt,
  };
}

function serializeMarketChart(c: MarketChartItem) {
  return {
    id: c.id,
    chart_slug: c.chartSlug,
    uploader_id: c.uploaderId,
    uploader_name: c.uploaderName,
    title: c.title,
    artist: c.artist,
    charter: c.charter,
    description: c.description,
    bpm: c.bpm,
    min_level: c.minLevel,
    max_level: c.maxLevel,
    tags: c.tags,
    difficulties: c.difficulties.map((d) => ({
      name: d.name,
      level: d.level,
      chart_file: d.chartFile,
      note_count: d.noteCount,
    })),
    package_sha256: c.packageSha256,
    content_hash: c.contentHash,
    package_size: c.packageSize,
    downloads: c.downloads,
    rating_avg: c.ratingAvg,
    rating_count: c.ratingCount,
    my_rating: c.myRating ?? null,
    created_at: c.createdAt,
    updated_at: c.updatedAt,
  };
}

function computeJsonETag(jsonBody: string): string {
  const digest = crypto.createHash("sha256").update(jsonBody, "utf8").digest("hex").slice(0, 32);
  return `"${digest}"`;
}

function stripETagDecoration(raw: string): string {
  let token = raw.trim();
  if (token.startsWith("W/") || token.startsWith("w/")) {
    token = token.slice(2).trim();
  }
  if (token.length >= 2 && token.startsWith('"') && token.endsWith('"')) {
    return token.slice(1, -1);
  }
  return token;
}

function ifNoneMatchMatches(ifNoneMatchHeader: string | undefined, etag: string): boolean {
  if (!ifNoneMatchHeader) return false;
  const trimmed = ifNoneMatchHeader.trim();
  if (!trimmed) return false;
  if (trimmed === "*") return true;
  const targetCore = stripETagDecoration(etag);
  for (const part of trimmed.split(",")) {
    const candidate = part.trim();
    if (!candidate) continue;
    if (candidate === "*") return true;
    if (stripETagDecoration(candidate) === targetCore) {
      return true;
    }
  }
  return false;
}

function respondJsonWithETag(c: Context, payload: unknown): Response {
  const jsonBody = JSON.stringify(payload);
  const etag = computeJsonETag(jsonBody);
  c.header("ETag", etag);
  if (ifNoneMatchMatches(c.req.header("If-None-Match"), etag)) {
    return c.body(null, 304);
  }
  return c.body(jsonBody, 200, {
    "Content-Type": "application/json; charset=UTF-8",
  });
}

function serializeArenaRoom(r: ArenaRoomRecord) {
  return {
    code: r.code,
    name: r.name,
    host_id: r.hostId,
    host_name: r.hostName,
    status: r.status,
    revision: r.revision,
    chart_id: r.chartId,
    chart_title: r.chartTitle,
    difficulty: r.difficulty,
    difficulty_level: r.difficultyLevel,
    chart_hash: r.chartHash,
    market_chart_id: r.marketChartId,
    package_size: r.packageSize,
    started_at: r.startedAt,
    updated_at: r.updatedAt,
    players: r.players.map((p) => ({
      user_id: p.userId,
      username: p.username,
      dan_badge: p.danBadge,
      is_host: p.isHost,
      ready: p.ready,
      finished: p.finished,
      connected: p.connected,
      last_seen_at: p.lastSeenAt,
      score: p.score,
      accuracy: p.accuracy,
      combo: p.combo,
      max_combo: p.maxCombo,
      grade: p.grade,
      placement: p.placement,
    })),
    messages: r.messages.map((m) => ({
      id: m.id,
      user_id: m.userId,
      username: m.username,
      phrase_id: m.phraseId,
      text: m.text,
      sent_at: m.sentAt,
    })),
  };
}

function arenaErrorStatus(msg: string): 403 | 404 | 409 | 422 | 429 {
  const lower = msg.toLowerCase();
  if (lower.includes("not found")) return 404;
  if (lower.includes("only the room host") || lower.includes("not joined")) return 403;
  if (lower.includes("room is full") || lower.includes("already playing")) return 409;
  if (lower.includes("cooldown") || lower.includes("rate limit")) return 429;
  return 422;
}

export interface AppOptions {
  storage: IStorageAdapter;
  jwtSecret: string;
  serverVersion?: string;
  nowProvider?: () => number;
}

function extractBearer(authHeader?: string): string | null {
  if (!authHeader) return null;
  const m = /^Bearer\s+(.+)$/i.exec(authHeader.trim());
  return m ? m[1].trim() : null;
}

function parseScoreSubmission(body: Record<string, unknown>): ScoreSubmission {
  const str = (a: string, b: string, def = ""): string => {
    const v = body[a] ?? body[b];
    return typeof v === "string" ? v : def;
  };
  const num = (a: string, b: string, def = 0): number => {
    const v = body[a] ?? body[b];
    return typeof v === "number" ? v : Number(v ?? def);
  };
  const bool = (a: string, b: string, def = false): boolean => {
    const v = body[a] ?? body[b];
    if (typeof v === "boolean") return v;
    if (typeof v === "number") return v !== 0;
    return def;
  };

  const replayRaw = body.replay_data ?? body.replayData;
  let replayData: string | undefined;
  if (typeof replayRaw === "string" && replayRaw.length > 0) {
    replayData = replayRaw;
  } else if (replayRaw && typeof replayRaw === "object") {
    replayData = JSON.stringify(replayRaw);
  }

  return {
    chartId: str("chart_id", "chartId"),
    chartTitle: str("chart_title", "chartTitle"),
    chartHash: str("chart_hash", "chartHash"),
    difficulty: str("difficulty", "difficulty"),
    difficultyLevel: num("difficulty_level", "difficultyLevel", 1.0),
    score: Math.trunc(num("score", "score", -1)),
    accuracy: num("accuracy", "accuracy", -1),
    maxCombo: Math.trunc(num("max_combo", "maxCombo", 0)),
    grade: str("grade", "grade", "D"),
    perfectCount: Math.trunc(num("perfect_count", "perfectCount", -1)),
    greatCount: Math.trunc(num("great_count", "greatCount", 0)),
    goodCount: Math.trunc(num("good_count", "goodCount", 0)),
    badCount: Math.trunc(num("bad_count", "badCount", 0)),
    missCount: Math.trunc(num("miss_count", "missCount", 0)),
    totalJudgments: Math.trunc(num("total_judgments", "totalJudgments", 0)),
    synergyCount: Math.trunc(num("synergy_count", "synergyCount", 0)),
    isFullCombo: bool("is_full_combo", "isFullCombo", false),
    isAllPerfect: bool("is_all_perfect", "isAllPerfect", false),
    assisted: bool("assisted", "assisted", false),
    playbackRate: num("playback_rate", "playbackRate", 1.0),
    replayData,
  };
}

export function createApp(opts: AppOptions): Hono {
  const app = new Hono();
  const { storage, jwtSecret } = opts;
  const serverVersion = opts.serverVersion || process.env.SAKURA_LATEST_VERSION || "v2.3.0";

  app.use("*", cors());

  const resolveAuth = (authHeader?: string): JwtPayload | null => {
    const token = extractBearer(authHeader);
    if (!token) return null;
    const payload = verifyJwt(token, jwtSecret);
    if (!payload) return null;
    const user = storage.findUserById(payload.sub);
    if (!user) return null;
    return payload;
  };

  app.get("/api/v1/health", (c) => {
    return c.json({
      ok: true,
      service: "sakura-online-server",
      mode: storage.mode,
      version: serverVersion,
      timestamp: Math.floor(Date.now() / 1000),
    });
  });

  app.get("/api/v1/version", (c) => {
    const tag = process.env.SAKURA_LATEST_VERSION || serverVersion;
    const normalizedTag = tag.startsWith("v") ? tag : `v${tag}`;
    const baseDownloadUrl = `https://github.com/StevenLee147/Sakura/releases/download/${normalizedTag}`;
    const installerAssetName = `Sakura-${normalizedTag}-win64-setup.exe`;
    const portableAssetName = `Sakura-${normalizedTag}-win64.zip`;
    const sha256AssetName = `Sakura-${normalizedTag}-SHA256SUMS.txt`;
    const installerExeUrl = `${baseDownloadUrl}/${installerAssetName}`;
    const portableZipUrl = `${baseDownloadUrl}/${portableAssetName}`;
    const sha256SumsUrl = `${baseDownloadUrl}/${sha256AssetName}`;
    return c.json({
      tag_name: normalizedTag,
      latest_version: normalizedTag,
      name: `Sakura-樱 ${normalizedTag}`,
      html_url: `https://github.com/StevenLee147/Sakura/releases/tag/${normalizedTag}`,
      download_url: installerExeUrl,
      portable_zip_url: portableZipUrl,
      installer_exe_url: installerExeUrl,
      sha256_sums_url: sha256SumsUrl,
      assets: [
        {
          name: installerAssetName,
          browser_download_url: installerExeUrl,
        },
        {
          name: portableAssetName,
          browser_download_url: portableZipUrl,
        },
        {
          name: sha256AssetName,
          browser_download_url: sha256SumsUrl,
        },
      ],
      body: "Sakura-樱 Online Ecosystem Update: Global Leaderboards, Replay Sharing, Sakura Rating & Auto-Updater.",
      published_at: new Date().toISOString(),
    });
  });

  const serializeAuthUser = (user: UserRecord) => ({
    id: user.id,
    username: user.username,
    is_guest: user.isGuest,
    total_pp: storage.getUserTotalPP(user.id),
    sakura_rating: storage.getUserSakuraRating(user.id),
    dan_badge: user.danBadge,
  });

  const issueSessionTokens = (user: UserRecord, deviceName = "") => {
    const { token, expiresAt } = signJwtWithExpiry(
      { sub: user.id, username: user.username, guest: user.isGuest },
      jwtSecret
    );
    const refreshToken = generateRefreshToken();
    const refreshExpiresAt = Math.floor(Date.now() / 1000) + REFRESH_TOKEN_TTL_SEC;
    storage.createAuthSession(user.id, hashRefreshToken(refreshToken), refreshExpiresAt, deviceName);
    return {
      token,
      refresh_token: refreshToken,
      expires_at: expiresAt,
      user: serializeAuthUser(user),
    };
  };

  const extractDeviceName = (body: Record<string, unknown>): string => {
    const raw = body.device_name ?? body.deviceName;
    return typeof raw === "string" ? raw : "";
  };

  app.post("/api/v1/auth/guest", async (c) => {
    let body: Record<string, unknown> = {};
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      body = {};
    }
    const nickname = typeof body.nickname === "string" ? body.nickname : undefined;
    const user = storage.createGuestUser(nickname);
    return c.json({
      ok: true,
      ...issueSessionTokens(user, extractDeviceName(body)),
    }, 201);
  });

  app.post("/api/v1/auth/register", async (c) => {
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const username = typeof body.username === "string" ? body.username.trim() : "";
    const password = typeof body.password === "string" ? body.password : "";
    if (!/^[A-Za-z0-9_\u4e00-\u9fa5-]{2,24}$/.test(username)) {
      return c.json({ ok: false, error: "Username must be 2-24 characters (letters, digits, _, -, or CJK)." }, 400);
    }
    if (password.length < 6 || password.length > 128) {
      return c.json({ ok: false, error: "Password must be between 6 and 128 characters." }, 400);
    }
    if (storage.findUserByUsername(username)) {
      return c.json({ ok: false, error: "Username already exists." }, 409);
    }
    const user = storage.registerUser(username, hashPassword(password));
    return c.json({
      ok: true,
      ...issueSessionTokens(user, extractDeviceName(body)),
    }, 201);
  });

  app.post("/api/v1/auth/login", async (c) => {
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const username = typeof body.username === "string" ? body.username.trim() : "";
    const password = typeof body.password === "string" ? body.password : "";
    const user = storage.findUserByUsername(username);
    if (!user || user.isGuest || !verifyPassword(password, user.passwordHash)) {
      return c.json({ ok: false, error: "Invalid username or password." }, 401);
    }
    return c.json({
      ok: true,
      ...issueSessionTokens(user, extractDeviceName(body)),
    });
  });

  app.post("/api/v1/auth/refresh", async (c) => {
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const rawRefresh = body.refresh_token ?? body.refreshToken;
    const oldRefreshToken = typeof rawRefresh === "string" ? rawRefresh.trim() : "";
    if (!oldRefreshToken) {
      return c.json({ ok: false, code: "session_expired", error: "Missing refresh token." }, 401);
    }
    const rawDevice = body.device_name ?? body.deviceName;
    const deviceName = typeof rawDevice === "string" ? rawDevice : undefined;

    const newRefreshToken = generateRefreshToken();
    const newRefreshExpiresAt = Math.floor(Date.now() / 1000) + REFRESH_TOKEN_TTL_SEC;
    const user = storage.rotateAuthSession(
      hashRefreshToken(oldRefreshToken),
      hashRefreshToken(newRefreshToken),
      newRefreshExpiresAt,
      deviceName
    );
    if (!user) {
      return c.json(
        { ok: false, code: "session_expired", error: "Refresh token is invalid, expired, or revoked." },
        401
      );
    }
    const { token, expiresAt } = signJwtWithExpiry(
      { sub: user.id, username: user.username, guest: user.isGuest },
      jwtSecret
    );
    return c.json({
      ok: true,
      token,
      refresh_token: newRefreshToken,
      expires_at: expiresAt,
      user: serializeAuthUser(user),
    });
  });

  app.post("/api/v1/auth/logout", async (c) => {
    let body: Record<string, unknown> = {};
    try {
      const parsed = await c.req.json();
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
        body = parsed as Record<string, unknown>;
      }
    } catch {
      body = {};
    }
    const rawRefresh = body.refresh_token ?? body.refreshToken;
    const refreshToken = typeof rawRefresh === "string" ? rawRefresh.trim() : "";
    const auth = resolveAuth(c.req.header("Authorization"));
    if (refreshToken) {
      storage.revokeAuthSession(hashRefreshToken(refreshToken));
    } else if (auth) {
      storage.revokeUserAuthSessions(auth.sub);
    }
    return c.json({ ok: true });
  });

  app.post("/api/v1/auth/upgrade-guest", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) {
      return c.json({ ok: false, error: "Unauthorized." }, 401);
    }
    const currentUser = storage.findUserById(auth.sub);
    if (!currentUser) {
      return c.json({ ok: false, error: "User not found." }, 401);
    }
    if (!currentUser.isGuest) {
      return c.json({ ok: false, error: "Account is already a registered user." }, 400);
    }
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const username = typeof body.username === "string" ? body.username.trim() : "";
    const password = typeof body.password === "string" ? body.password : "";
    if (!/^[A-Za-z0-9_\u4e00-\u9fa5-]{2,24}$/.test(username)) {
      return c.json({ ok: false, error: "Username must be 2-24 characters (letters, digits, _, -, or CJK)." }, 400);
    }
    if (password.length < 6 || password.length > 128) {
      return c.json({ ok: false, error: "Password must be between 6 and 128 characters." }, 400);
    }
    const existing = storage.findUserByUsername(username);
    if (existing && existing.id !== currentUser.id) {
      return c.json({ ok: false, error: "Username already exists." }, 409);
    }
    try {
      const updatedUser = storage.upgradeGuestUser(currentUser.id, username, hashPassword(password));
      const rawOldRefresh = body.refresh_token ?? body.refreshToken;
      if (typeof rawOldRefresh === "string" && rawOldRefresh.trim().length > 0) {
        storage.revokeAuthSession(hashRefreshToken(rawOldRefresh.trim()));
      }
      return c.json({
        ok: true,
        ...issueSessionTokens(updatedUser, extractDeviceName(body)),
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to upgrade guest account.";
      if (msg.toLowerCase().includes("already exists")) {
        return c.json({ ok: false, error: msg }, 409);
      }
      return c.json({ ok: false, error: msg }, 400);
    }
  });

  app.post("/api/v1/auth/change-password", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) {
      return c.json({ ok: false, error: "Unauthorized." }, 401);
    }
    const user = storage.findUserById(auth.sub);
    if (!user) {
      return c.json({ ok: false, error: "User not found." }, 401);
    }
    if (user.isGuest) {
      return c.json({ ok: false, error: "Guest accounts cannot change password." }, 400);
    }
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const rawOld = body.old_password ?? body.oldPassword;
    const rawNew = body.new_password ?? body.newPassword;
    const oldPassword = typeof rawOld === "string" ? rawOld : "";
    const newPassword = typeof rawNew === "string" ? rawNew : "";
    if (!oldPassword || !verifyPassword(oldPassword, user.passwordHash)) {
      return c.json({ ok: false, error: "Incorrect current password." }, 401);
    }
    if (newPassword.length < 6 || newPassword.length > 128) {
      return c.json({ ok: false, error: "New password must be between 6 and 128 characters." }, 400);
    }
    storage.updateUserPassword(user.id, hashPassword(newPassword));
    storage.revokeUserAuthSessions(user.id);
    const refreshedUser = storage.findUserById(user.id)!;
    return c.json({
      ok: true,
      ...issueSessionTokens(refreshedUser, extractDeviceName(body)),
    });
  });

  app.get("/api/v1/auth/me", (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) {
      return c.json({ ok: false, error: "Unauthorized." }, 401);
    }
    const user = storage.findUserById(auth.sub);
    if (!user) {
      return c.json({ ok: false, error: "User not found." }, 401);
    }
    return c.json({
      ok: true,
      user: {
        id: user.id,
        username: user.username,
        is_guest: user.isGuest,
        total_pp: storage.getUserTotalPP(user.id),
        sakura_rating: storage.getUserSakuraRating(user.id),
        dan_badge: user.danBadge,
      },
    });
  });

  app.post("/api/v1/scores", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) {
      return c.json({ ok: false, error: "Unauthorized." }, 401);
    }
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const sub = parseScoreSubmission(body);
      const res = storage.submitScore(auth.sub, sub);
      const totalPp = storage.getUserTotalPP(auth.sub);
      const sakuraRating = storage.getUserSakuraRating(auth.sub);
      return c.json({
        ok: true,
        score_id: res.scoreId,
        pp: res.pp,
        rank: res.rank,
        new_best: res.newBest,
        total_pp: totalPp,
        sakura_rating: sakuraRating,
      }, 201);
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Score validation failed.";
      return c.json({ ok: false, error: msg }, 422);
    }
  });

  app.get("/api/v1/leaderboards/:chartId", (c) => {
    const chartId = c.req.param("chartId");
    const difficulty = c.req.query("difficulty") || "Normal";
    const scope = c.req.query("scope") || "global";
    const limit = Number(c.req.query("limit") || "50");
    const auth = resolveAuth(c.req.header("Authorization"));
    const lb = storage.getLeaderboard(chartId, difficulty, Number.isFinite(limit) ? limit : 50, auth?.sub);
    return respondJsonWithETag(c, {
      ok: true,
      chart_id: chartId,
      difficulty,
      scope,
      my_rank: lb.myRank,
      entries: lb.entries.map(serializeLeaderboardEntry),
    });
  });

  app.get("/api/v1/daily", (c) => {
    try {
      const spec = computeDailyChallenge(c.req.query("date"));
      const limit = Number(c.req.query("limit") || "20");
      const auth = resolveAuth(c.req.header("Authorization"));
      const lb = storage.getDailyLeaderboard(spec.date, Number.isFinite(limit) ? limit : 20, auth?.sub);
      return respondJsonWithETag(c, {
        ok: true,
        daily: {
          date: spec.date,
          chart_id: spec.chartId,
          chart_title: spec.chartTitle,
          difficulty: spec.difficulty,
          difficulty_level: spec.difficultyLevel,
          goal_text: spec.goalText,
          target_score: spec.targetScore,
        },
        my_rank: lb.myRank,
        entries: lb.entries.map(serializeLeaderboardEntry),
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Invalid daily challenge query.";
      return c.json({ ok: false, error: msg }, 400);
    }
  });

  app.post("/api/v1/daily/scores", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) {
      return c.json({ ok: false, error: "Unauthorized." }, 401);
    }
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const rawDate = body.date ?? body.date_utc8 ?? body.dateUtc8 ?? c.req.query("date");
      const dateStr = typeof rawDate === "string" ? rawDate : undefined;
      const sub = parseScoreSubmission(body);
      const res = storage.submitDailyScore(auth.sub, sub, dateStr);
      return c.json({
        ok: true,
        date: res.date,
        rank: res.rank,
        new_best: res.newBest,
        sakura_rating: storage.getUserSakuraRating(auth.sub),
      }, 201);
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Daily score validation failed.";
      return c.json({ ok: false, error: msg }, 422);
    }
  });

  app.get("/api/v1/daily/leaderboard", (c) => {
    try {
      const spec = computeDailyChallenge(c.req.query("date"));
      const limit = Number(c.req.query("limit") || "50");
      const auth = resolveAuth(c.req.header("Authorization"));
      const lb = storage.getDailyLeaderboard(spec.date, Number.isFinite(limit) ? limit : 50, auth?.sub);
      return respondJsonWithETag(c, {
        ok: true,
        date: spec.date,
        my_rank: lb.myRank,
        entries: lb.entries.map(serializeLeaderboardEntry),
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Invalid daily leaderboard query.";
      return c.json({ ok: false, error: msg }, 400);
    }
  });

  app.get("/api/v1/replays/:scoreId", (c) => {
    const scoreId = c.req.param("scoreId");
    const data = storage.getReplayData(scoreId);
    if (!data) {
      return c.json({ ok: false, error: "Replay not found." }, 404);
    }
    return c.body(data, 200, {
      "Content-Type": "application/json; charset=utf-8",
    });
  });

  app.post("/api/v1/charts", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    const user = storage.findUserById(auth.sub);
    if (!user) return c.json({ ok: false, error: "User not found." }, 401);

    try {
      const ct = (c.req.header("Content-Type") || "").toLowerCase();
      let pkgBuffer: Buffer;
      let description = c.req.query("description") || "";
      if (ct.includes("application/json")) {
        const body = (await c.req.json()) as Record<string, unknown>;
        const b64 = typeof body.data_base64 === "string" ? body.data_base64 : "";
        if (!b64) return c.json({ ok: false, error: "Missing data_base64 in JSON body." }, 400);
        if (typeof body.description === "string") description = body.description;
        pkgBuffer = Buffer.from(b64, "base64");
      } else {
        const ab = await c.req.arrayBuffer();
        pkgBuffer = Buffer.from(ab);
      }
      if (pkgBuffer.length === 0) {
        return c.json({ ok: false, error: "Empty .sakura package body." }, 400);
      }
      const item = storage.publishChart(user.id, user.username, pkgBuffer, description);
      return c.json({ ok: true, chart: serializeMarketChart(item) }, 201);
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Chart validation failed.";
      return c.json({ ok: false, error: msg }, 422);
    }
  });

  app.get("/api/v1/charts", (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    const q = c.req.query("q") || "";
    const tag = c.req.query("tag") || "";
    const sortRaw = c.req.query("sort") || "newest";
    const sort = sortRaw === "popular" || sortRaw === "rating" ? sortRaw : "newest";
    const minRaw = c.req.query("minLevel");
    const maxRaw = c.req.query("maxLevel");
    const page = Number(c.req.query("page") || "1");
    const pageSize = Number(c.req.query("pageSize") || "20");
    const res = storage.listCharts({
      q,
      tag,
      sort,
      minLevel: minRaw !== undefined && minRaw !== "" ? Number(minRaw) : undefined,
      maxLevel: maxRaw !== undefined && maxRaw !== "" ? Number(maxRaw) : undefined,
      page: Number.isFinite(page) ? page : 1,
      pageSize: Number.isFinite(pageSize) ? pageSize : 20,
      currentUserId: auth?.sub,
    });
    return respondJsonWithETag(c, {
      ok: true,
      total: res.total,
      page: res.page,
      page_size: res.pageSize,
      charts: res.charts.map(serializeMarketChart),
    });
  });

  app.get("/api/v1/charts/:id", (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    const detail = storage.getChartById(c.req.param("id"), auth?.sub);
    if (!detail) return c.json({ ok: false, error: "Chart not found." }, 404);
    return c.json({
      ok: true,
      chart: serializeMarketChart(detail.chart),
      ratings: detail.ratings.map((r) => ({
        user_id: r.userId,
        username: r.username,
        rating: r.rating,
        comment: r.comment,
        updated_at: r.updatedAt,
      })),
    });
  });

  app.get("/api/v1/charts/:id/download", (c) => {
    const dl = storage.downloadChartPackage(c.req.param("id"));
    if (!dl) return c.json({ ok: false, error: "Chart package not found." }, 404);
    return new Response(new Uint8Array(dl.buffer), {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Length": String(dl.buffer.length),
        "X-Sakura-Sha256": dl.chart.packageSha256,
        "Content-Disposition": `attachment; filename="${dl.chart.id}.sakura"`,
      },
    });
  });

  app.post("/api/v1/charts/:id/rate", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    const user = storage.findUserById(auth.sub);
    if (!user) return c.json({ ok: false, error: "User not found." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const rating = Number(body.rating);
      const comment = typeof body.comment === "string" ? body.comment : "";
      const res = storage.rateChart(c.req.param("id"), user.id, user.username, rating, comment);
      return c.json({
        ok: true,
        rating_avg: res.ratingAvg,
        rating_count: res.ratingCount,
        my_rating: res.myRating,
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to rate chart.";
      return c.json({ ok: false, error: msg }, msg.includes("not found") ? 404 : 422);
    }
  });

  app.post("/api/v1/sync/push", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    const rawScores = Array.isArray(body.scores) ? body.scores : [];
    const scores: CloudScoreRecord[] = rawScores.map((raw) => {
      const r = (raw || {}) as Record<string, unknown>;
      return {
        chartId: String(r.chart_id ?? r.chartId ?? ""),
        chartTitle: String(r.chart_title ?? r.chartTitle ?? ""),
        chartHash: String(r.chart_hash ?? r.chartHash ?? ""),
        difficulty: String(r.difficulty ?? "Normal"),
        difficultyLevel: Number(r.difficulty_level ?? r.difficultyLevel ?? 1),
        score: Math.trunc(Number(r.score ?? 0)),
        accuracy: Number(r.accuracy ?? 0),
        maxCombo: Math.trunc(Number(r.max_combo ?? r.maxCombo ?? 0)),
        grade: String(r.grade ?? "D"),
        perfectCount: Math.trunc(Number(r.perfect_count ?? r.perfectCount ?? 0)),
        greatCount: Math.trunc(Number(r.great_count ?? r.greatCount ?? 0)),
        goodCount: Math.trunc(Number(r.good_count ?? r.goodCount ?? 0)),
        badCount: Math.trunc(Number(r.bad_count ?? r.badCount ?? 0)),
        missCount: Math.trunc(Number(r.miss_count ?? r.missCount ?? 0)),
        isFullCombo: Boolean(r.is_full_combo ?? r.isFullCombo ?? false),
        isAllPerfect: Boolean(r.is_all_perfect ?? r.isAllPerfect ?? false),
        playedAt: Math.trunc(Number(r.played_at ?? r.playedAt ?? 0)),
      };
    });
    const rawAchs = Array.isArray(body.achievements) ? body.achievements : [];
    const achievements: CloudAchievementRecord[] = rawAchs.map((raw) => {
      const a = (raw || {}) as Record<string, unknown>;
      return {
        id: String(a.id ?? ""),
        unlockedAt: Math.trunc(Number(a.unlocked_at ?? a.unlockedAt ?? 0)),
      };
    });
    const rawCoursesInput = body.course_records ?? body.courseRecords;
    const rawCourses = Array.isArray(rawCoursesInput) ? rawCoursesInput : [];
    const courseRecords: CloudCourseRecord[] = rawCourses.map((raw) => {
      const cr = (raw || {}) as Record<string, unknown>;
      return {
        courseId: String(cr.course_id ?? cr.courseId ?? ""),
        badgeText: String(cr.badge_text ?? cr.badgeText ?? ""),
        danRank: Math.trunc(Number(cr.dan_rank ?? cr.danRank ?? 0)),
        totalScore: Math.trunc(Number(cr.total_score ?? cr.totalScore ?? 0)),
        avgAccuracy: Number(cr.avg_accuracy ?? cr.avgAccuracy ?? 0),
        remainingLife: Number(cr.remaining_life ?? cr.remainingLife ?? 0),
        cleared: Boolean(cr.cleared ?? false),
        clearedAt: Math.trunc(Number(cr.cleared_at ?? cr.clearedAt ?? 0)),
      };
    });
    const settings = body.settings && typeof body.settings === "object" && !Array.isArray(body.settings)
      ? (body.settings as Record<string, unknown>)
      : undefined;
    const settingsUpdatedAt = Number(body.settings_updated_at ?? body.settingsUpdatedAt ?? 0);

    const merged = storage.pushCloudSync(auth.sub, {
      scores,
      achievements,
      courseRecords,
      settings,
      settingsUpdatedAt,
    });
    return c.json({
      ok: true,
      synced_at: merged.snapshot.syncedAt,
      scores_merged: merged.scoresMerged,
      achievements_merged: merged.achievementsMerged,
      courses_merged: merged.coursesMerged,
      scores: merged.snapshot.scores.map((s) => ({
        chart_id: s.chartId,
        chart_title: s.chartTitle,
        chart_hash: s.chartHash,
        difficulty: s.difficulty,
        difficulty_level: s.difficultyLevel,
        score: s.score,
        accuracy: s.accuracy,
        max_combo: s.maxCombo,
        grade: s.grade,
        perfect_count: s.perfectCount,
        great_count: s.greatCount,
        good_count: s.goodCount,
        bad_count: s.badCount,
        miss_count: s.missCount,
        is_full_combo: s.isFullCombo,
        is_all_perfect: s.isAllPerfect,
        played_at: s.playedAt,
      })),
      achievements: merged.snapshot.achievements.map((a) => ({ id: a.id, unlocked_at: a.unlockedAt })),
      course_records: merged.snapshot.courseRecords.map((cr) => ({
        course_id: cr.courseId,
        badge_text: cr.badgeText,
        dan_rank: cr.danRank,
        total_score: cr.totalScore,
        avg_accuracy: cr.avgAccuracy,
        remaining_life: cr.remainingLife,
        cleared: cr.cleared,
        cleared_at: cr.clearedAt,
      })),
      settings: merged.snapshot.settings,
      settings_updated_at: merged.snapshot.settingsUpdatedAt,
    });
  });

  app.get("/api/v1/sync/pull", (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    const snap = storage.pullCloudSync(auth.sub);
    return c.json({
      ok: true,
      synced_at: snap.syncedAt,
      scores: snap.scores.map((s) => ({
        chart_id: s.chartId,
        chart_title: s.chartTitle,
        chart_hash: s.chartHash,
        difficulty: s.difficulty,
        difficulty_level: s.difficultyLevel,
        score: s.score,
        accuracy: s.accuracy,
        max_combo: s.maxCombo,
        grade: s.grade,
        perfect_count: s.perfectCount,
        great_count: s.greatCount,
        good_count: s.goodCount,
        bad_count: s.badCount,
        miss_count: s.missCount,
        is_full_combo: s.isFullCombo,
        is_all_perfect: s.isAllPerfect,
        played_at: s.playedAt,
      })),
      achievements: snap.achievements.map((a) => ({ id: a.id, unlocked_at: a.unlockedAt })),
      course_records: snap.courseRecords.map((cr) => ({
        course_id: cr.courseId,
        badge_text: cr.badgeText,
        dan_rank: cr.danRank,
        total_score: cr.totalScore,
        avg_accuracy: cr.avgAccuracy,
        remaining_life: cr.remainingLife,
        cleared: cr.cleared,
        cleared_at: cr.clearedAt,
      })),
      settings: snap.settings,
      settings_updated_at: snap.settingsUpdatedAt,
    });
  });

  const resolveArenaNow = (c: Context): { nowSec?: number; nowMs?: number } => {
    const rawMs = c.req.query("now_ms") ?? c.req.header("X-Sakura-Now-Ms");
    const rawSec = c.req.query("now") ?? c.req.query("now_sec") ?? c.req.header("X-Sakura-Now");
    if (rawMs !== undefined && rawMs.trim() !== "") {
      const ms = Number(rawMs);
      if (Number.isFinite(ms) && ms > 0) {
        return { nowMs: Math.trunc(ms), nowSec: Math.floor(ms / 1000) };
      }
    }
    if (rawSec !== undefined && rawSec.trim() !== "") {
      const sec = Number(rawSec);
      if (Number.isFinite(sec) && sec > 0) {
        return { nowSec: Math.floor(sec), nowMs: Math.floor(sec * 1000) };
      }
    }
    if (opts.nowProvider) {
      const ms = opts.nowProvider();
      return { nowMs: Math.trunc(ms), nowSec: Math.floor(ms / 1000) };
    }
    return {};
  };

  app.post("/api/v1/rooms", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const { nowSec } = resolveArenaNow(c);
      const nameRaw = body.name ?? body.room_name ?? body.roomName;
      const chartIdRaw = body.chart_id ?? body.chartId;
      const chartTitleRaw = body.chart_title ?? body.chartTitle;
      const difficultyRaw = body.difficulty ?? body.difficulty_name ?? body.difficultyName;
      const diffLevelRaw = body.difficulty_level ?? body.difficultyLevel;
      const chartHashRaw = body.chart_hash ?? body.chartHash;
      const marketChartIdRaw = body.market_chart_id ?? body.marketChartId;

      const room = storage.createArenaRoom(
        auth.sub,
        {
          name: typeof nameRaw === "string" ? nameRaw : "",
          chartId: typeof chartIdRaw === "string" ? chartIdRaw : "",
          chartTitle: typeof chartTitleRaw === "string" ? chartTitleRaw : "",
          difficulty: typeof difficultyRaw === "string" ? difficultyRaw : "Normal",
          difficultyLevel: diffLevelRaw !== undefined ? Number(diffLevelRaw) : 1.0,
          chartHash: typeof chartHashRaw === "string" ? chartHashRaw : "",
          marketChartId: typeof marketChartIdRaw === "string" ? marketChartIdRaw : "",
        },
        nowSec
      );
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized }, 201);
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to create arena room.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.get("/api/v1/rooms", (c) => {
    const { nowSec } = resolveArenaNow(c);
    const limit = Number(c.req.query("limit") || "50");
    const rooms = storage.listArenaRooms(Number.isFinite(limit) ? limit : 50, nowSec);
    return c.json({
      ok: true,
      total: rooms.length,
      rooms: rooms.map(serializeArenaRoom),
    });
  });

  app.post("/api/v1/rooms/:code/join", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    try {
      const { nowSec } = resolveArenaNow(c);
      const room = storage.joinArenaRoom(c.req.param("code"), auth.sub, nowSec);
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to join arena room.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.post("/api/v1/rooms/:code/leave", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    try {
      const { nowSec } = resolveArenaNow(c);
      const room = storage.leaveArenaRoom(c.req.param("code"), auth.sub, nowSec);
      if (!room) {
        return c.json({ ok: true, deleted: true, room: null });
      }
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, deleted: false, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to leave arena room.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.post("/api/v1/rooms/:code/select", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const { nowSec } = resolveArenaNow(c);
      const chartIdRaw = body.chart_id ?? body.chartId;
      const chartTitleRaw = body.chart_title ?? body.chartTitle;
      const difficultyRaw = body.difficulty ?? body.difficulty_name ?? body.difficultyName;
      const diffLevelRaw = body.difficulty_level ?? body.difficultyLevel;
      const chartHashRaw = body.chart_hash ?? body.chartHash;
      const marketChartIdRaw = body.market_chart_id ?? body.marketChartId;

      const room = storage.selectArenaChart(
        c.req.param("code"),
        auth.sub,
        {
          chartId: typeof chartIdRaw === "string" ? chartIdRaw : "",
          chartTitle: typeof chartTitleRaw === "string" ? chartTitleRaw : "",
          difficulty: typeof difficultyRaw === "string" ? difficultyRaw : "",
          difficultyLevel: diffLevelRaw !== undefined ? Number(diffLevelRaw) : 1.0,
          chartHash: typeof chartHashRaw === "string" ? chartHashRaw : "",
          marketChartId: typeof marketChartIdRaw === "string" ? marketChartIdRaw : "",
        },
        nowSec
      );
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to select arena chart.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.post("/api/v1/rooms/:code/ready", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const { nowSec } = resolveArenaNow(c);
      const startRaw = body.start_match ?? body.startMatch ?? body.start;
      const startMatch = startRaw === true || startRaw === 1;
      const readyRaw = body.ready;
      const ready =
        typeof readyRaw === "boolean"
          ? readyRaw
          : typeof readyRaw === "number"
            ? readyRaw !== 0
            : startMatch;
      const room = storage.setArenaReady(c.req.param("code"), auth.sub, ready, startMatch, nowSec);
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to update ready state.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.post("/api/v1/rooms/:code/progress", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const { nowSec } = resolveArenaNow(c);
      const scoreRaw = body.score;
      const accRaw = body.accuracy;
      const comboRaw = body.combo;
      const maxComboRaw = body.max_combo ?? body.maxCombo;
      const gradeRaw = body.grade;
      const finRaw = body.finished ?? body.is_finished ?? body.isFinished;

      const score = typeof scoreRaw === "number" ? scoreRaw : Number(scoreRaw ?? -1);
      const accuracy = typeof accRaw === "number" ? accRaw : Number(accRaw ?? 0);
      const combo = typeof comboRaw === "number" ? comboRaw : Number(comboRaw ?? 0);
      const maxCombo = typeof maxComboRaw === "number" ? maxComboRaw : Number(maxComboRaw ?? combo);
      const grade = typeof gradeRaw === "string" ? gradeRaw : "D";
      const finished =
        typeof finRaw === "boolean" ? finRaw : typeof finRaw === "number" ? finRaw !== 0 : false;

      const room = storage.reportArenaProgress(
        c.req.param("code"),
        auth.sub,
        {
          score,
          accuracy,
          combo,
          maxCombo,
          grade,
          finished,
        },
        nowSec
      );
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to report arena progress.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  app.post("/api/v1/rooms/:code/chat", async (c) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    if (!auth) return c.json({ ok: false, error: "Unauthorized." }, 401);
    let body: Record<string, unknown>;
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      return c.json({ ok: false, error: "Invalid JSON body." }, 400);
    }
    try {
      const { nowSec, nowMs } = resolveArenaNow(c);
      const rawPhrase = body.phrase_id ?? body.phraseId;
      const phraseId =
        typeof rawPhrase === "number"
          ? rawPhrase
          : typeof rawPhrase === "string" && rawPhrase.trim() !== ""
            ? Number(rawPhrase)
            : NaN;
      const room = storage.sendArenaChat(c.req.param("code"), auth.sub, phraseId, nowSec, nowMs);
      const serialized = serializeArenaRoom(room);
      return c.json({ ok: true, room: serialized, ...serialized });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Failed to send arena quick chat.";
      return c.json({ ok: false, error: msg }, arenaErrorStatus(msg));
    }
  });

  const handleGetArenaRoomState = (c: Context) => {
    const auth = resolveAuth(c.req.header("Authorization"));
    const { nowSec } = resolveArenaNow(c);
    const room = storage.getArenaRoomState(c.req.param("code") || "", auth?.sub, nowSec);
    if (!room) {
      return c.json({ ok: false, error: "Room not found." }, 404);
    }
    const sinceRaw = c.req.query("since_revision");
    if (sinceRaw !== undefined && sinceRaw.trim() !== "") {
      const sinceRev = Number(sinceRaw);
      if (Number.isFinite(sinceRev) && sinceRev === room.revision) {
        return c.json({ ok: true, unchanged: true, revision: room.revision });
      }
    }
    const serialized = serializeArenaRoom(room);
    return c.json({ ok: true, room: serialized, ...serialized });
  };

  app.get("/api/v1/rooms/:code", handleGetArenaRoomState);
  app.get("/api/v1/rooms/:code/state", handleGetArenaRoomState);

  return app;
}
