import { Hono } from "hono";
import { cors } from "hono/cors";
import { hashPassword, JwtPayload, signJwt, verifyJwt, verifyPassword } from "./auth.js";
import { CloudAchievementRecord, CloudScoreRecord, IStorageAdapter, MarketChartItem, ScoreSubmission } from "./storage/types.js";

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
  const serverVersion = opts.serverVersion || process.env.SAKURA_LATEST_VERSION || "v1.3.0";

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
    return c.json({
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
    const settings = body.settings && typeof body.settings === "object" && !Array.isArray(body.settings)
      ? (body.settings as Record<string, unknown>)
      : undefined;
    const settingsUpdatedAt = Number(body.settings_updated_at ?? body.settingsUpdatedAt ?? 0);

    const merged = storage.pushCloudSync(auth.sub, { scores, achievements, settings, settingsUpdatedAt });
    return c.json({
      ok: true,
      synced_at: merged.snapshot.syncedAt,
      scores_merged: merged.scoresMerged,
      achievements_merged: merged.achievementsMerged,
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
      settings: snap.settings,
      settings_updated_at: snap.settingsUpdatedAt,
    });
  });

  return app;
}
