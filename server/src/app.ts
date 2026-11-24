import { Hono } from "hono";
import { cors } from "hono/cors";
import { hashPassword, JwtPayload, signJwt, verifyJwt, verifyPassword } from "./auth.js";
import { IStorageAdapter, ScoreSubmission } from "./storage/types.js";

export interface AppOptions {
  storage: IStorageAdapter;
  jwtSecret: string;
  serverVersion?: string;
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
  const serverVersion = opts.serverVersion || process.env.SAKURA_LATEST_VERSION || "v1.1.0";

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
    return c.json({
      tag_name: normalizedTag,
      latest_version: normalizedTag,
      name: `Sakura-樱 ${normalizedTag}`,
      html_url: `https://github.com/StevenLee147/Sakura/releases/tag/${normalizedTag}`,
      download_url: `https://github.com/StevenLee147/Sakura/releases/download/${normalizedTag}/Sakura-${normalizedTag}-win64-setup.exe`,
      body: "Sakura-樱 Online Ecosystem Update: Global Leaderboards, Replay Sharing & Auto-Updater.",
      published_at: new Date().toISOString(),
    });
  });

  app.post("/api/v1/auth/guest", async (c) => {
    let body: Record<string, unknown> = {};
    try {
      body = (await c.req.json()) as Record<string, unknown>;
    } catch {
      body = {};
    }
    const nickname = typeof body.nickname === "string" ? body.nickname : undefined;
    const user = storage.createGuestUser(nickname);
    const token = signJwt({ sub: user.id, username: user.username, guest: true }, jwtSecret);
    return c.json({
      ok: true,
      token,
      user: {
        id: user.id,
        username: user.username,
        is_guest: true,
        total_pp: 0,
      },
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
    const token = signJwt({ sub: user.id, username: user.username, guest: false }, jwtSecret);
    return c.json({
      ok: true,
      token,
      user: {
        id: user.id,
        username: user.username,
        is_guest: false,
        total_pp: 0,
      },
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
    const token = signJwt({ sub: user.id, username: user.username, guest: false }, jwtSecret);
    const totalPp = storage.getUserTotalPP(user.id);
    return c.json({
      ok: true,
      token,
      user: {
        id: user.id,
        username: user.username,
        is_guest: false,
        total_pp: totalPp,
      },
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
      return c.json({
        ok: true,
        score_id: res.scoreId,
        pp: res.pp,
        rank: res.rank,
        new_best: res.newBest,
        total_pp: totalPp,
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
    return c.json({
      ok: true,
      chart_id: chartId,
      difficulty,
      scope,
      my_rank: lb.myRank,
      entries: lb.entries.map((e) => ({
        rank: e.rank,
        score_id: e.scoreId,
        user_id: e.userId,
        username: e.username,
        score: e.score,
        accuracy: e.accuracy,
        max_combo: e.maxCombo,
        grade: e.grade,
        pp: e.pp,
        is_full_combo: e.isFullCombo,
        is_all_perfect: e.isAllPerfect,
        has_replay: e.hasReplay,
        played_at: e.playedAt,
      })),
    });
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

  return app;
}
