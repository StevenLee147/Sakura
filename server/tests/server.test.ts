import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import test from "node:test";
import { computeDailyChallenge, createApp } from "../src/app.js";
import { DEFAULT_JWT_TTL_SEC, hashRefreshToken, REFRESH_TOKEN_TTL_SEC, verifyJwt } from "../src/auth.js";
import { computeChartRating, PostgresCompatibleAdapter, SqliteStorageAdapter } from "../src/storage/index.js";
import { createStoreZipArchive } from "../src/zip_validator.js";

test("Sakura Online Server v1.1.0 Auth, Anti-Cheat Scores, Leaderboards & Replays", async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "sakura-srv-test-"));
  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret: "unit-test-secret", serverVersion: "v1.1.0" });

  try {
    // 1. Health & version
    const healthRes = await app.request("/api/v1/health");
    assert.equal(healthRes.status, 200);
    const healthJson = (await healthRes.json()) as { ok: boolean; mode: string; version: string };
    assert.equal(healthJson.ok, true);
    assert.equal(healthJson.mode, "sqlite");

    const verRes = await app.request("/api/v1/version");
    assert.equal(verRes.status, 200);
    const verJson = (await verRes.json()) as { tag_name: string };
    assert.equal(verJson.tag_name, "v1.1.0");

    // 2. Guest creation
    const guestRes = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "SakuraGuest" }),
    });
    assert.equal(guestRes.status, 201);
    const guestData = (await guestRes.json()) as { token: string; user: { username: string; is_guest: boolean } };
    assert.equal(guestData.user.is_guest, true);
    assert.match(guestData.user.username, /^SakuraGuest_/);

    // 3. Register + Login + Me
    const regRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "Kaguya", password: "sakura12345" }),
    });
    assert.equal(regRes.status, 201);

    const dupRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "kaguya", password: "sakura12345" }),
    });
    assert.equal(dupRes.status, 409);

    const loginRes = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "Kaguya", password: "sakura12345" }),
    });
    assert.equal(loginRes.status, 200);
    const loginData = (await loginRes.json()) as { token: string; user: { id: string; username: string } };

    const meRes = await app.request("/api/v1/auth/me", {
      headers: { Authorization: `Bearer ${loginData.token}` },
    });
    assert.equal(meRes.status, 200);

    // 4. Reject assisted or tampered scores
    const assistedRes = await app.request("/api/v1/scores", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${loginData.token}`,
      },
      body: JSON.stringify({
        chart_id: "sakura_breath",
        chart_hash: "123456789",
        difficulty: "Expert",
        difficulty_level: 9.5,
        score: 995000,
        accuracy: 99.5,
        max_combo: 200,
        grade: "S",
        perfect_count: 198,
        great_count: 2,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        assisted: true,
      }),
    });
    assert.equal(assistedRes.status, 422);

    const overCapRes = await app.request("/api/v1/scores", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${loginData.token}`,
      },
      body: JSON.stringify({
        chart_id: "sakura_breath",
        chart_hash: "123456789",
        difficulty: "Expert",
        difficulty_level: 9.5,
        score: 1050000,
        accuracy: 100.0,
        max_combo: 200,
        grade: "SS",
        perfect_count: 200,
        great_count: 0,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        assisted: false,
      }),
    });
    assert.equal(overCapRes.status, 422);

    // 5. Submit valid score + .skr replay for registered user & guest user
    const replayPayload = JSON.stringify({
      format: "sakura-replay",
      version: 2,
      chart_id: "sakura_breath",
      chart_hash: "123456789",
      difficulty: 2,
      rate: 1.0,
      start_ms: 0,
      inputs: [[1000, 0, 0, 0, 0], [1080, 1, 0, 0, 0]],
    });

    const score1Res = await app.request("/api/v1/scores", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${guestData.token}`,
      },
      body: JSON.stringify({
        chart_id: "sakura_breath",
        chart_title: "Sakura Breath",
        chart_hash: "123456789",
        difficulty: "Expert",
        difficulty_level: 9.5,
        score: 960000,
        accuracy: 97.5,
        max_combo: 180,
        grade: "S",
        perfect_count: 190,
        great_count: 10,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
        replay_data: replayPayload,
      }),
    });
    assert.equal(score1Res.status, 201);

    const score2Res = await app.request("/api/v1/scores", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${loginData.token}`,
      },
      body: JSON.stringify({
        chart_id: "sakura_breath",
        chart_title: "Sakura Breath",
        chart_hash: "123456789",
        difficulty: "Expert",
        difficulty_level: 9.5,
        score: 998500,
        accuracy: 99.85,
        max_combo: 200,
        grade: "S",
        perfect_count: 199,
        great_count: 1,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        synergy_count: 6,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
        replay_data: replayPayload,
      }),
    });
    assert.equal(score2Res.status, 201);
    const score2Json = (await score2Res.json()) as { score_id: string; rank: number; pp: number; new_best: boolean };
    assert.equal(score2Json.rank, 1);
    assert.equal(score2Json.new_best, true);
    assert.ok(score2Json.pp > 250);

    // 6. Leaderboard deduplication & rank check
    const lbRes = await app.request("/api/v1/leaderboards/sakura_breath?difficulty=Expert&limit=10", {
      headers: { Authorization: `Bearer ${guestData.token}` },
    });
    assert.equal(lbRes.status, 200);
    const lbJson = (await lbRes.json()) as {
      my_rank: number;
      entries: Array<{ rank: number; username: string; score: number; has_replay: boolean; score_id: string }>;
    };
    assert.equal(lbJson.my_rank, 2);
    assert.equal(lbJson.entries.length, 2);
    assert.equal(lbJson.entries[0].username, "Kaguya");
    assert.equal(lbJson.entries[0].score, 998500);
    assert.equal(lbJson.entries[0].has_replay, true);

    // 7. Replay download
    const repRes = await app.request(`/api/v1/replays/${score2Json.score_id}`);
    assert.equal(repRes.status, 200);
    const repText = await repRes.text();
    assert.equal(repText, replayPayload);
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }

  // Dual-mode PostgresCompatibleAdapter check
  const pgTmp = fs.mkdtempSync(path.join(os.tmpdir(), "sakura-pg-test-"));
  const pgAdapter = new PostgresCompatibleAdapter("postgres://localhost:5432/sakura", pgTmp);
  try {
    assert.equal(pgAdapter.mode, "postgres");
  } finally {
    pgAdapter.close();
    fs.rmSync(pgTmp, { recursive: true, force: true });
  }
});

test("Sakura Online Server v1.2.0 Chart Market & Cloud Sync", async () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "sakura-v120-test-"));
  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret: "v120-test-secret" });

  try {
    const regRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "CharterAoi", password: "sakura123456" }),
    });
    assert.equal(regRes.status, 201);
    const { token } = (await regRes.json()) as { token: string };

    const guestRes = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "Reviewer" }),
    });
    const { token: guestToken } = (await guestRes.json()) as { token: string };

    // Reject path-traversal archive
    const badZip = createStoreZipArchive({
      "../evil.txt": "nope",
    });
    const badUpload = await app.request("/api/v1/charts", {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/octet-stream" },
      body: new Uint8Array(badZip),
    });
    assert.equal(badUpload.status, 422);

    // Build valid .sakura archive
    const infoJson = JSON.stringify({
      id: "moonlit_bloom",
      title: "Moonlit Bloom",
      artist: "Sakura Sound Team",
      charter: "CharterAoi",
      bpm: 176,
      audio: "track.wav",
      tags: ["electronic", "featured"],
      difficulties: [
        { name: "Normal", level: 4.5, chart_file: "normal.json" },
        { name: "Master", level: 11.2, chart_file: "master.json" },
      ],
    });
    const normalChart = JSON.stringify({ offset: 0, bpm: 176, keyboard_notes: [{ lane: 0, time: 500, duration: 0 }, { lane: 2, time: 900, duration: 0 }], mouse_notes: [] });
    const masterChart = JSON.stringify({ version: 3, offset: 0, bpm: 176, keyboard_notes: [{ lane: 1, time: 400, duration: 0 }, { lane: 4, time: 700, duration: 300, link_id: 1 }], mouse_notes: [{ time: 700, x: 0.5, y: 0.5, slider_duration: 0, link_id: 1, require_lane: 4 }] });
    const validZip = createStoreZipArchive({
      "info.json": infoJson,
      "normal.json": normalChart,
      "master.json": masterChart,
      "track.wav": "RIFF____WAVEfmt ",
    });
    const expectedSha256 = crypto.createHash("sha256").update(validZip).digest("hex");

    const upRes = await app.request("/api/v1/charts?description=Atmospheric%20stream%20chart", {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/octet-stream" },
      body: new Uint8Array(validZip),
    });
    assert.equal(upRes.status, 201);
    const upJson = (await upRes.json()) as { chart: { id: string; package_sha256: string; max_level: number } };
    assert.equal(upJson.chart.id, "moonlit_bloom");
    assert.equal(upJson.chart.package_sha256, expectedSha256);
    assert.equal(upJson.chart.max_level, 11.2);

    // Search & filter
    const listRes = await app.request("/api/v1/charts?q=Moonlit&tag=featured&minLevel=10&sort=newest");
    assert.equal(listRes.status, 200);
    const listJson = (await listRes.json()) as { total: number; charts: Array<{ id: string }> };
    assert.equal(listJson.total, 1);
    assert.equal(listJson.charts[0].id, "moonlit_bloom");

    // Rate & idempotent update
    const rate1 = await app.request("/api/v1/charts/moonlit_bloom/rate", {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ rating: 5, comment: "Super fun flow!" }),
    });
    assert.equal(rate1.status, 200);

    const rate2 = await app.request("/api/v1/charts/moonlit_bloom/rate", {
      method: "POST",
      headers: { Authorization: `Bearer ${guestToken}`, "Content-Type": "application/json" },
      body: JSON.stringify({ rating: 3, comment: "Tricky ending" }),
    });
    assert.equal(rate2.status, 200);
    const rate2Json = (await rate2.json()) as { rating_avg: number; rating_count: number };
    assert.equal(rate2Json.rating_count, 2);
    assert.equal(rate2Json.rating_avg, 4.0);

    // Download & verify SHA-256 + download count increment
    const dlRes = await app.request("/api/v1/charts/moonlit_bloom/download");
    assert.equal(dlRes.status, 200);
    assert.equal(dlRes.headers.get("X-Sakura-Sha256"), expectedSha256);
    const dlBytes = Buffer.from(await dlRes.arrayBuffer());
    assert.equal(crypto.createHash("sha256").update(dlBytes).digest("hex"), expectedSha256);

    const detailRes = await app.request("/api/v1/charts/moonlit_bloom");
    const detailJson = (await detailRes.json()) as { chart: { downloads: number }; ratings: unknown[] };
    assert.equal(detailJson.chart.downloads, 1);
    assert.equal(detailJson.ratings.length, 2);

    // Cloud Sync idempotent merge
    const push1 = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [{
          chart_id: "moonlit_bloom", chart_title: "Moonlit Bloom", chart_hash: "hash_mb_m",
          difficulty: "Master", difficulty_level: 11.2, score: 950000, accuracy: 96.4,
          max_combo: 120, grade: "S", perfect_count: 110, great_count: 10, good_count: 0, bad_count: 0, miss_count: 0,
          is_full_combo: true, is_all_perfect: false, played_at: 1700000100,
        }],
        achievements: [{ id: "first_clear", unlocked_at: 1700000200 }],
        settings: { note_speed: 6.5, audio_offset: -8 },
        settings_updated_at: 1700000300,
      }),
    });
    assert.equal(push1.status, 200);

    // Push lower score + earlier achievement timestamp -> keeps higher score (950000) & earliest unlock (1700000050)
    const push2 = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [{
          chart_id: "moonlit_bloom", chart_title: "Moonlit Bloom", chart_hash: "hash_mb_m",
          difficulty: "Master", difficulty_level: 11.2, score: 910000, accuracy: 93.0,
          max_combo: 90, grade: "A", perfect_count: 95, great_count: 25, good_count: 0, bad_count: 0, miss_count: 0,
          is_full_combo: false, is_all_perfect: false, played_at: 1700000400,
        }],
        achievements: [
          { id: "first_clear", unlocked_at: 1700000050 },
          { id: "full_combo_1", unlocked_at: 1700000500 },
        ],
        settings: { note_speed: 5.0 },
        settings_updated_at: 1700000100,
      }),
    });
    assert.equal(push2.status, 200);

    const pullRes = await app.request("/api/v1/sync/pull", {
      headers: { Authorization: `Bearer ${token}` },
    });
    assert.equal(pullRes.status, 200);
    const pullJson = (await pullRes.json()) as {
      scores: Array<{ score: number; is_full_combo: boolean }>;
      achievements: Array<{ id: string; unlocked_at: number }>;
      settings: { note_speed: number };
    };
    assert.equal(pullJson.scores.length, 1);
    assert.equal(pullJson.scores[0].score, 950000);
    assert.equal(pullJson.scores[0].is_full_combo, true);
    assert.equal(pullJson.achievements.length, 2);
    assert.equal(pullJson.achievements[0].id, "first_clear");
    assert.equal(pullJson.achievements[0].unlocked_at, 1700000050);
    assert.equal(pullJson.settings.note_speed, 6.5);
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test("Sakura Online Server v1.4.0 Daily Challenge, Dan Badge Sync & Multi-Collection Union Sync", async () => {
  const tmpDir = path.join(process.cwd(), `.sakura-v140-test-${crypto.randomUUID()}`);
  fs.mkdirSync(tmpDir, { recursive: true });

  // Pre-seed legacy users table without dan_badge to verify PRAGMA migration
  {
    const legacyDb = new DatabaseSync(path.join(tmpDir, "online.db"));
    legacyDb.exec(`
      CREATE TABLE users (
        id TEXT PRIMARY KEY,
        username TEXT NOT NULL UNIQUE COLLATE NOCASE,
        password_hash TEXT NOT NULL,
        is_guest INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL
      );
    `);
    legacyDb.close();
  }

  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret: "v140-test-secret", serverVersion: "v1.4.0" });

  try {
    // 1. Deterministic FNV-1a Daily Challenge validation
    const defaultSpec = computeDailyChallenge();
    assert.match(defaultSpec.date, /^\d{4}-\d{2}-\d{2}$/);
    const specA = computeDailyChallenge("2026-11-26");
    const specA2 = computeDailyChallenge("2026-11-26");
    assert.deepEqual(specA, specA2);
    assert.throws(() => computeDailyChallenge("2026/11/26"));

    const badDailyRes = await app.request("/api/v1/daily?date=bad-date");
    assert.equal(badDailyRes.status, 400);

    // 2. Register + Guest auth include dan_badge
    const regRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraKaiden", password: "sakura123456" }),
    });
    assert.equal(regRes.status, 201);
    const regJson = (await regRes.json()) as {
      token: string;
      user: { id: string; username: string; dan_badge: string };
    };
    assert.equal(regJson.user.dan_badge, "");

    const guestRes = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "Challenger" }),
    });
    assert.equal(guestRes.status, 201);
    const guestJson = (await guestRes.json()) as {
      token: string;
      user: { dan_badge: string };
    };
    assert.equal(guestJson.user.dan_badge, "");

    // 3. Dan badge sync (highest cleared dan_rank wins; failed higher dan does not overwrite)
    const syncDan1 = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [],
        achievements: [],
        course_records: [
          {
            course_id: "dan_shodan",
            badge_text: "初段",
            dan_rank: 1,
            total_score: 2850000,
            avg_accuracy: 96.5,
            remaining_life: 72.0,
            cleared: true,
            cleared_at: 1700010000,
          },
          {
            course_id: "dan_kaiden",
            badge_text: "皆传",
            dan_rank: 11,
            total_score: 1800000,
            avg_accuracy: 88.0,
            remaining_life: 0.0,
            cleared: false,
            cleared_at: 0,
          },
        ],
        settings: {
          note_speed: 6.5,
          collections: [
            { id: "col_fav", name: "Favorites", chart_ids: ["sakura_awakening", "starry_sky"], updated_at: 1700010000 },
            { id: "col_dojo", name: "Dojo Prep", chart_ids: ["instant_bloom"], updated_at: 1700010000 },
          ],
        },
        settings_updated_at: 1700010000,
      }),
    });
    assert.equal(syncDan1.status, 200);

    const meAfterShodan = await app.request("/api/v1/auth/me", {
      headers: { Authorization: `Bearer ${regJson.token}` },
    });
    const meShodanJson = (await meAfterShodan.json()) as { user: { dan_badge: string } };
    assert.equal(meShodanJson.user.dan_badge, "初段");

    // Clear Kaiden (rank 11) and also push lower Nidan (rank 2) + multi-collection union on timestamp tie
    const syncDan2 = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [],
        achievements: [],
        course_records: [
          {
            course_id: "dan_kaiden",
            badge_text: "皆传",
            dan_rank: 11,
            total_score: 2965000,
            avg_accuracy: 99.1,
            remaining_life: 44.5,
            cleared: true,
            cleared_at: 1700020000,
          },
          {
            course_id: "dan_nidan",
            badge_text: "二段",
            dan_rank: 2,
            total_score: 2900000,
            avg_accuracy: 97.8,
            remaining_life: 85.0,
            cleared: true,
            cleared_at: 1700020500,
          },
        ],
        settings: {
          note_speed: 7.0,
          collections: [
            { id: "col_fav", name: "Favorites", chart_ids: ["starry_sky", "flowing_petals"], updated_at: 1700010000 },
            { id: "col_chill", name: "Chill", chart_ids: ["sakura_breath"], updated_at: 1700010000 },
          ],
        },
        settings_updated_at: 1700010000,
      }),
    });
    assert.equal(syncDan2.status, 200);

    const loginRes = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraKaiden", password: "sakura123456" }),
    });
    const loginJson = (await loginRes.json()) as { user: { dan_badge: string } };
    assert.equal(loginJson.user.dan_badge, "皆传");

    const pullRes = await app.request("/api/v1/sync/pull", {
      headers: { Authorization: `Bearer ${regJson.token}` },
    });
    assert.equal(pullRes.status, 200);
    const pullJson = (await pullRes.json()) as {
      course_records: Array<{ course_id: string; badge_text: string; dan_rank: number; cleared: boolean }>;
      settings: {
        note_speed: number;
        collections: Array<{ id: string; name: string; chart_ids: string[] }>;
      };
    };
    assert.equal(pullJson.course_records.length, 3);
    const kaidenRec = pullJson.course_records.find((c) => c.course_id === "dan_kaiden");
    assert.ok(kaidenRec);
    assert.equal(kaidenRec.cleared, true);
    assert.equal(pullJson.settings.note_speed, 7.0);
    assert.equal(pullJson.settings.collections.length, 3);
    const favCol = pullJson.settings.collections.find((c) => c.id === "col_fav");
    assert.ok(favCol);
    assert.deepEqual(favCol.chart_ids, ["sakura_awakening", "starry_sky", "flowing_petals"]);

    // 4. Daily Challenge GET, POST score validation, ranking & dan_badge exposure
    const targetDate = "2026-11-26";
    const dailyGet1 = await app.request(`/api/v1/daily?date=${targetDate}&limit=20`);
    assert.equal(dailyGet1.status, 200);
    const dailyInfo1 = (await dailyGet1.json()) as {
      ok: boolean;
      daily: {
        date: string;
        chart_id: string;
        chart_title: string;
        difficulty: string;
        difficulty_level: number;
        goal_text: string;
        target_score: number;
      };
      my_rank: number | null;
      entries: unknown[];
    };
    assert.equal(dailyInfo1.ok, true);
    assert.equal(dailyInfo1.daily.date, targetDate);
    assert.equal(dailyInfo1.daily.chart_id, specA.chartId);
    assert.equal(dailyInfo1.daily.difficulty, specA.difficulty);
    assert.equal(dailyInfo1.my_rank, null);
    assert.equal(dailyInfo1.entries.length, 0);

    // Reject wrong chart/difficulty on daily endpoint
    const mismatchRes = await app.request("/api/v1/daily/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        date: targetDate,
        chart_id: "wrong_chart_id",
        chart_hash: "hash_daily",
        difficulty: specA.difficulty,
        difficulty_level: specA.difficultyLevel,
        score: 980000,
        accuracy: 99.0,
        max_combo: 200,
        grade: "S",
        perfect_count: 198,
        great_count: 2,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        assisted: false,
      }),
    });
    assert.equal(mismatchRes.status, 422);

    // Submit valid daily scores for guest & Kaiden user
    const guestDailySubmit = await app.request("/api/v1/daily/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${guestJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        date: targetDate,
        chart_id: specA.chartId,
        chart_title: specA.chartTitle,
        chart_hash: "hash_daily",
        difficulty: specA.difficulty,
        difficulty_level: specA.difficultyLevel,
        score: 952000,
        accuracy: 97.2,
        max_combo: 175,
        grade: "S",
        perfect_count: 188,
        great_count: 12,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(guestDailySubmit.status, 201);
    const guestDailyJson = (await guestDailySubmit.json()) as { ok: boolean; date: string; rank: number; new_best: boolean };
    assert.equal(guestDailyJson.ok, true);
    assert.equal(guestDailyJson.date, targetDate);
    assert.equal(guestDailyJson.rank, 1);
    assert.equal(guestDailyJson.new_best, true);

    const kaidenDailySubmit = await app.request("/api/v1/daily/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        date: targetDate,
        chart_id: specA.chartId,
        chart_title: specA.chartTitle,
        chart_hash: "hash_daily",
        difficulty: specA.difficulty,
        difficulty_level: specA.difficultyLevel,
        score: 992000,
        accuracy: 99.6,
        max_combo: 200,
        grade: "S",
        perfect_count: 199,
        great_count: 1,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(kaidenDailySubmit.status, 201);
    const kaidenDailyJson = (await kaidenDailySubmit.json()) as { ok: boolean; date: string; rank: number; new_best: boolean };
    assert.equal(kaidenDailyJson.rank, 1);
    assert.equal(kaidenDailyJson.new_best, true);

    const dailyLbRes = await app.request(`/api/v1/daily/leaderboard?date=${targetDate}&limit=50`, {
      headers: { Authorization: `Bearer ${guestJson.token}` },
    });
    assert.equal(dailyLbRes.status, 200);
    const dailyLbJson = (await dailyLbRes.json()) as {
      ok: boolean;
      date: string;
      my_rank: number;
      entries: Array<{ rank: number; username: string; dan_badge: string; score: number }>;
    };
    assert.equal(dailyLbJson.ok, true);
    assert.equal(dailyLbJson.date, targetDate);
    assert.equal(dailyLbJson.my_rank, 2);
    assert.equal(dailyLbJson.entries.length, 2);
    assert.equal(dailyLbJson.entries[0].username, "SakuraKaiden");
    assert.equal(dailyLbJson.entries[0].dan_badge, "皆传");
    assert.equal(dailyLbJson.entries[1].dan_badge, "");

    // Also verify standard chart leaderboard exposes dan_badge
    const normalScoreRes = await app.request("/api/v1/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        chart_id: specA.chartId,
        chart_title: specA.chartTitle,
        chart_hash: "hash_std",
        difficulty: specA.difficulty,
        difficulty_level: specA.difficultyLevel,
        score: 995000,
        accuracy: 99.7,
        max_combo: 200,
        grade: "S",
        perfect_count: 199,
        great_count: 1,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 200,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(normalScoreRes.status, 201);

    const stdLbRes = await app.request(`/api/v1/leaderboards/${specA.chartId}?difficulty=${specA.difficulty}`);
    const stdLbJson = (await stdLbRes.json()) as { entries: Array<{ username: string; dan_badge: string }> };
    assert.equal(stdLbJson.entries[0].username, "SakuraKaiden");
    assert.equal(stdLbJson.entries[0].dan_badge, "皆传");
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test("Sakura Online Server v1.6.0 Multiplayer Arena Rooms & Podium Ranking", async () => {
  const tmpDir = path.join(process.cwd(), `.sakura-v160-test-${crypto.randomUUID()}`);
  fs.mkdirSync(tmpDir, { recursive: true });
  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret: "v160-test-secret", serverVersion: "v1.6.0" });

  try {
    // Verify real-song Daily Challenge pool contains no deleted procedural song IDs
    const deletedProcedural = new Set(["spring_breeze", "cherry_blossom", "digital_dream"]);
    for (let day = 1; day <= 60; day++) {
      const d = `2026-${String(Math.ceil(day / 30)).padStart(2, "0")}-${String(((day - 1) % 28) + 1).padStart(2, "0")}`;
      const spec = computeDailyChallenge(d);
      assert.equal(deletedProcedural.has(spec.chartId), false);
    }

    // 1. Register host (with Kaiden badge) + 2 contenders
    const hostReg = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "ArenaHost", password: "sakura123456" }),
    });
    assert.equal(hostReg.status, 201);
    const hostAuth = (await hostReg.json()) as { token: string; user: { id: string; username: string } };

    await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [],
        achievements: [],
        course_records: [{
          course_id: "dan_kaiden",
          badge_text: "皆传",
          dan_rank: 11,
          total_score: 2980000,
          avg_accuracy: 99.4,
          remaining_life: 60,
          cleared: true,
          cleared_at: 1700050000,
        }],
      }),
    });

    const p2Reg = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "RivalRin", password: "sakura123456" }),
    });
    assert.equal(p2Reg.status, 201);
    const p2Auth = (await p2Reg.json()) as { token: string; user: { id: string; username: string } };

    const p3Guest = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "PetalMiku" }),
    });
    assert.equal(p3Guest.status, 201);
    const p3Auth = (await p3Guest.json()) as { token: string; user: { id: string; username: string } };

    // 2. Create Arena room (201, 6-char uppercase alphanumeric code, host ready=true)
    type RoomJson = {
      code: string;
      name: string;
      host_id: string;
      host_name: string;
      status: string;
      chart_id: string;
      chart_title: string;
      difficulty: string;
      difficulty_level: number;
      chart_hash: string;
      started_at: number;
      updated_at: number;
      players: Array<{
        user_id: string;
        username: string;
        dan_badge: string;
        is_host: boolean;
        ready: boolean;
        finished: boolean;
        score: number;
        accuracy: number;
        combo: number;
        max_combo: number;
        grade: string;
        placement: number;
      }>;
    };

    const createRes = await app.request("/api/v1/rooms", {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        name: "Sakura Grand Finals",
        chart_id: "sakura_awakening",
        chart_title: "樱光初醒",
        difficulty: "Hard",
        difficulty_level: 7.0,
        chart_hash: "hash_sa_h",
      }),
    });
    assert.equal(createRes.status, 201);
    const createdBody = (await createRes.json()) as { ok: boolean; room: RoomJson } & RoomJson;
    assert.equal(createdBody.ok, true);
    const code = createdBody.code;
    assert.match(code, /^[A-Z0-9]{6}$/);
    assert.equal(createdBody.status, "waiting");
    assert.equal(createdBody.host_id, hostAuth.user.id);
    assert.equal(createdBody.players.length, 1);
    assert.equal(createdBody.players[0].is_host, true);
    assert.equal(createdBody.players[0].ready, true);
    assert.equal(createdBody.players[0].dan_badge, "皆传");
    assert.equal(createdBody.players[0].placement, 1);

    // Seed an expired room (>7200s old) and verify GET /api/v1/rooms filters it out
    storage.db.prepare(`
      INSERT INTO arena_rooms (code, name, host_id, host_name, status, chart_id, chart_title, difficulty, difficulty_level, chart_hash, started_at, updated_at)
      VALUES ('OLD999', 'Expired Lobby', ?, 'ArenaHost', 'waiting', 'starry_sky', '繁星', 'Hard', 6.5, '', 0, ?)
    `).run(hostAuth.user.id, Math.floor(Date.now() / 1000) - 8000);

    const listRes1 = await app.request("/api/v1/rooms");
    assert.equal(listRes1.status, 200);
    const listBody1 = (await listRes1.json()) as { ok: boolean; rooms: RoomJson[] };
    assert.equal(listBody1.rooms.length, 1);
    assert.equal(listBody1.rooms[0].code, code);

    // 3. Join room (case-insensitive code + idempotent re-join)
    const joinP2 = await app.request(`/api/v1/rooms/${code.toLowerCase()}/join`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}` },
    });
    assert.equal(joinP2.status, 200);

    const joinP2Again = await app.request(`/api/v1/rooms/${code}/join`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}` },
    });
    assert.equal(joinP2Again.status, 200);
    const afterIdempotentJoin = (await joinP2Again.json()) as RoomJson;
    assert.equal(afterIdempotentJoin.players.length, 2);

    const joinP3 = await app.request(`/api/v1/rooms/${code}/join`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p3Auth.token}` },
    });
    assert.equal(joinP3.status, 200);
    const afterP3Join = (await joinP3.json()) as RoomJson;
    assert.equal(afterP3Join.players.length, 3);

    // 4. Host-only chart selection
    const nonHostSelect = await app.request(`/api/v1/rooms/${code}/select`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        chart_id: "instant_bloom",
        chart_title: "一瞬花绽",
        difficulty: "Hard",
        difficulty_level: 9.0,
      }),
    });
    assert.equal(nonHostSelect.status, 403);

    const hostSelect = await app.request(`/api/v1/rooms/${code}/select`, {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        chart_id: "instant_bloom",
        chart_title: "一瞬花绽",
        difficulty: "Hard",
        difficulty_level: 9.0,
        chart_hash: "hash_ib_hard",
      }),
    });
    assert.equal(hostSelect.status, 200);
    const selectedRoom = (await hostSelect.json()) as RoomJson;
    assert.equal(selectedRoom.chart_id, "instant_bloom");
    assert.equal(selectedRoom.chart_title, "一瞬花绽");
    assert.equal(selectedRoom.difficulty_level, 9.0);
    assert.equal(selectedRoom.status, "waiting");

    // 5. Ready up & host starts match
    const rdyP2 = await app.request(`/api/v1/rooms/${code}/ready`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ ready: true }),
    });
    assert.equal(rdyP2.status, 200);

    const rdyP3 = await app.request(`/api/v1/rooms/${code}/ready`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p3Auth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ ready: true }),
    });
    assert.equal(rdyP3.status, 200);

    const startRes = await app.request(`/api/v1/rooms/${code}/ready`, {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ ready: true, start_match: true }),
    });
    assert.equal(startRes.status, 200);
    const startedRoom = (await startRes.json()) as RoomJson;
    assert.equal(startedRoom.status, "playing");
    assert.ok(startedRoom.started_at > 0);

    // 6. Live progress validation & podium ranking (score DESC, accuracy DESC, updated_at ASC)
    const invalidProg = await app.request(`/api/v1/rooms/${code}/progress`, {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ score: 1000001, accuracy: 100.0, combo: 10, max_combo: 10, grade: "SS", finished: false }),
    });
    assert.equal(invalidProg.status, 422);

    // RivalRin finishes with 992,000 (99.50%)
    const progP2 = await app.request(`/api/v1/rooms/${code}/progress`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ score: 992000, accuracy: 99.5, combo: 310, max_combo: 310, grade: "S", finished: true }),
    });
    assert.equal(progP2.status, 200);
    assert.equal(((await progP2.json()) as RoomJson).status, "playing");

    // ArenaHost finishes with 985,000 (98.90%) — tied score with PetalMiku but higher accuracy
    const progHost = await app.request(`/api/v1/rooms/${code}/progress`, {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ score: 985000, accuracy: 98.9, combo: 280, max_combo: 295, grade: "S", finished: true }),
    });
    assert.equal(progHost.status, 200);
    assert.equal(((await progHost.json()) as RoomJson).status, "playing");

    // PetalMiku finishes last with 985,000 (97.40%) -> all finished => room transitions to 'finished'
    const progP3 = await app.request(`/api/v1/rooms/${code}/progress`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p3Auth.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ score: 985000, accuracy: 97.4, combo: 240, max_combo: 260, grade: "S", finished: true }),
    });
    assert.equal(progP3.status, 200);
    const finishedRoom = (await progP3.json()) as RoomJson;
    assert.equal(finishedRoom.status, "finished");

    const stateRes = await app.request(`/api/v1/rooms/${code}/state`);
    assert.equal(stateRes.status, 200);
    const podium = (await stateRes.json()) as RoomJson;
    assert.equal(podium.status, "finished");
    assert.equal(podium.players.length, 3);
    assert.deepEqual(
      podium.players.map((p) => ({ user_id: p.user_id, placement: p.placement, score: p.score })),
      [
        { user_id: p2Auth.user.id, placement: 1, score: 992000 },
        { user_id: hostAuth.user.id, placement: 2, score: 985000 },
        { user_id: p3Auth.user.id, placement: 3, score: 985000 },
      ]
    );

    // Finished rooms should not appear in open lobby listing
    const listAfterFinish = await app.request("/api/v1/rooms");
    const listAfterBody = (await listAfterFinish.json()) as { rooms: RoomJson[] };
    assert.equal(listAfterBody.rooms.length, 0);

    // 7. Host leaves -> next player promoted to host; all leave -> room deleted
    const hostLeave = await app.request(`/api/v1/rooms/${code}/leave`, {
      method: "POST",
      headers: { Authorization: `Bearer ${hostAuth.token}` },
    });
    assert.equal(hostLeave.status, 200);
    const afterHostLeave = (await hostLeave.json()) as { deleted: boolean; room: RoomJson };
    assert.equal(afterHostLeave.deleted, false);
    assert.equal(afterHostLeave.room.players.length, 2);
    assert.notEqual(afterHostLeave.room.host_id, hostAuth.user.id);
    const promoted = afterHostLeave.room.players.find((p) => p.user_id === afterHostLeave.room.host_id);
    assert.ok(promoted);
    assert.equal(promoted.is_host, true);

    await app.request(`/api/v1/rooms/${code}/leave`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p2Auth.token}` },
    });
    const finalLeave = await app.request(`/api/v1/rooms/${code}/leave`, {
      method: "POST",
      headers: { Authorization: `Bearer ${p3Auth.token}` },
    });
    assert.equal(finalLeave.status, 200);
    const finalLeaveBody = (await finalLeave.json()) as { deleted: boolean; room: RoomJson | null };
    assert.equal(finalLeaveBody.deleted, true);
    assert.equal(finalLeaveBody.room, null);

    const missingState = await app.request(`/api/v1/rooms/${code}/state`);
    assert.equal(missingState.status, 404);
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});

test("Sakura Online Server v2.0.0 Authoritative Sakura Rating & Updater Assets", async () => {
  const tmpDir = path.join(process.cwd(), `.sakura-v200-test-${crypto.randomUUID()}`);
  fs.mkdirSync(tmpDir, { recursive: true });
  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret: "v200-test-secret", serverVersion: "2.0.0" });

  try {
    // 1. Direct computeChartRating curve & bonus verification across all brackets
    assert.equal(computeChartRating(0, 100, true, true, 50), 0);
    assert.equal(computeChartRating(-2.5, 99.9, true, false, 10), 0);
    assert.ok(Math.abs(computeChartRating(10.0, 100.0, true, true, 40) - 12.18) < 1e-9);
    assert.ok(Math.abs(computeChartRating(10.0, 99.75, true, false, 10) - 11.83) < 1e-9);
    assert.ok(Math.abs(computeChartRating(9.0, 98.6, false, false, 0) - 10.2) < 1e-9);
    assert.ok(Math.abs(computeChartRating(9.0, 96.5, false, false, 0) - 9.5) < 1e-9);
    assert.ok(Math.abs(computeChartRating(8.0, 92.5, false, false, 0) - 7.5) < 1e-9);
    assert.ok(Math.abs(computeChartRating(10.0, 45.0, false, false, 0) - 2.25) < 1e-9);
    assert.equal(computeChartRating(0.5, 80.0, false, false, 0), 0);

    // 2. /api/v1/version dual-artifact + SHA256SUMS URLs & GitHub-compatible assets array
    const verRes = await app.request("/api/v1/version");
    assert.equal(verRes.status, 200);
    const verJson = (await verRes.json()) as {
      tag_name: string;
      latest_version: string;
      download_url: string;
      portable_zip_url: string;
      installer_exe_url: string;
      sha256_sums_url: string;
      assets: Array<{ name: string; browser_download_url: string }>;
    };
    assert.equal(verJson.tag_name, "v2.0.0");
    assert.equal(verJson.latest_version, "v2.0.0");
    assert.match(verJson.installer_exe_url, /Sakura-v2\.0\.0-win64-setup\.exe$/);
    assert.equal(verJson.download_url, verJson.installer_exe_url);
    assert.match(verJson.portable_zip_url, /Sakura-v2\.0\.0-win64\.zip$/);
    assert.match(verJson.sha256_sums_url, /Sakura-v2\.0\.0-SHA256SUMS\.txt$/);
    assert.equal(verJson.assets.length, 3);
    assert.deepEqual(
      verJson.assets.map((a) => a.name),
      [
        "Sakura-v2.0.0-win64-setup.exe",
        "Sakura-v2.0.0-win64.zip",
        "Sakura-v2.0.0-SHA256SUMS.txt",
      ]
    );
    assert.equal(verJson.assets[0].browser_download_url, verJson.installer_exe_url);
    assert.equal(verJson.assets[1].browser_download_url, verJson.portable_zip_url);
    assert.equal(verJson.assets[2].browser_download_url, verJson.sha256_sums_url);

    // 3. Guest & Register initialize sakura_rating to 0
    const guestRes = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "BudGuest" }),
    });
    assert.equal(guestRes.status, 201);
    const guestJson = (await guestRes.json()) as { token: string; user: { sakura_rating: number } };
    assert.equal(guestJson.user.sakura_rating, 0);

    const regRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraMaster", password: "sakura123456" }),
    });
    assert.equal(regRes.status, 201);
    const regJson = (await regRes.json()) as { token: string; user: { id: string; sakura_rating: number } };
    assert.equal(regJson.user.sakura_rating, 0);

    // 4. Cloud-synced scores contribute to B30 and fallback rawR10 = b30Avg when recent scores is empty
    const syncRes = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        scores: [
          {
            chart_id: "starry_sky",
            chart_title: "繁星",
            chart_hash: "hash_ss_ex",
            difficulty: "Expert",
            difficulty_level: 10.0,
            score: 1000000,
            accuracy: 100.0,
            max_combo: 500,
            grade: "SS",
            perfect_count: 500,
            great_count: 0,
            good_count: 0,
            bad_count: 0,
            miss_count: 0,
            is_full_combo: true,
            is_all_perfect: true,
            played_at: 1700100000,
          },
          {
            chart_id: "sakura_breath",
            chart_title: "樱落呼吸",
            chart_hash: "hash_sb_hd",
            difficulty: "Hard",
            difficulty_level: 8.0,
            score: 980000,
            accuracy: 98.0,
            max_combo: 320,
            grade: "S",
            perfect_count: 310,
            great_count: 10,
            good_count: 0,
            bad_count: 0,
            miss_count: 0,
            is_full_combo: true,
            is_all_perfect: false,
            played_at: 1700100100,
          },
        ],
        achievements: [],
      }),
    });
    assert.equal(syncRes.status, 200);

    // (12.12 + 9.06) / 2 = 10.59
    const meAfterSync = await app.request("/api/v1/auth/me", {
      headers: { Authorization: `Bearer ${regJson.token}` },
    });
    assert.equal(meAfterSync.status, 200);
    const meSyncJson = (await meAfterSync.json()) as { user: { sakura_rating: number } };
    assert.equal(meSyncJson.user.sakura_rating, 10.59);

    // 5. Ranked score upgrades (sakura_breath, Hard) from 9.06 -> 10.14; B30=11.13, R10=10.14 => 10.88
    const upScoreRes = await app.request("/api/v1/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        chart_id: "sakura_breath",
        chart_title: "樱落呼吸",
        chart_hash: "hash_sb_hd",
        difficulty: "Hard",
        difficulty_level: 8.0,
        score: 1000000,
        accuracy: 100.0,
        max_combo: 320,
        grade: "SS",
        perfect_count: 320,
        great_count: 0,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 320,
        synergy_count: 10,
        is_full_combo: true,
        is_all_perfect: true,
        assisted: false,
      }),
    });
    assert.equal(upScoreRes.status, 201);
    const upScoreJson = (await upScoreRes.json()) as { sakura_rating: number };
    assert.equal(upScoreJson.sakura_rating, 10.88);

    // 6. Anti-degradation floor: 10 low-accuracy plays on starry_sky do not lower B30 (11.13)
    // and floor R10 at b30Avg * 0.85 = 9.4605 => Math.round((0.75 * 11.13 + 0.25 * 9.4605) * 100) / 100 = 10.71
    for (let i = 0; i < 10; i++) {
      const lowRes = await app.request("/api/v1/scores", {
        method: "POST",
        headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
        body: JSON.stringify({
          chart_id: "starry_sky",
          chart_title: "繁星",
          chart_hash: "hash_ss_ex",
          difficulty: "Expert",
          difficulty_level: 10.0,
          score: 450000,
          accuracy: 45.0,
          max_combo: 40,
          grade: "D",
          perfect_count: 45,
          great_count: 0,
          good_count: 0,
          bad_count: 0,
          miss_count: 55,
          total_judgments: 100,
          synergy_count: 0,
          is_full_combo: false,
          is_all_perfect: false,
          assisted: false,
        }),
      });
      assert.equal(lowRes.status, 201);
    }

    const loginRes = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraMaster", password: "sakura123456" }),
    });
    assert.equal(loginRes.status, 200);
    const loginJson = (await loginRes.json()) as { user: { sakura_rating: number } };
    assert.equal(loginJson.user.sakura_rating, 10.71);

    // 7. Leaderboards & Daily Challenge entries expose sakura_rating
    const lbRes = await app.request("/api/v1/leaderboards/sakura_breath?difficulty=Hard");
    assert.equal(lbRes.status, 200);
    const lbJson = (await lbRes.json()) as { entries: Array<{ username: string; sakura_rating: number }> };
    assert.equal(lbJson.entries.length, 1);
    assert.equal(lbJson.entries[0].username, "SakuraMaster");
    assert.equal(lbJson.entries[0].sakura_rating, 10.71);

    const targetDate = "2026-11-26";
    const dailySpec = computeDailyChallenge(targetDate);
    const dailySubmit = await app.request("/api/v1/daily/scores", {
      method: "POST",
      headers: { Authorization: `Bearer ${regJson.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({
        date: targetDate,
        chart_id: dailySpec.chartId,
        chart_title: dailySpec.chartTitle,
        chart_hash: "hash_daily_v200",
        difficulty: dailySpec.difficulty,
        difficulty_level: dailySpec.difficultyLevel,
        score: 990000,
        accuracy: 99.5,
        max_combo: 250,
        grade: "S",
        perfect_count: 248,
        great_count: 2,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 250,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(dailySubmit.status, 201);

    const dailyGet = await app.request(`/api/v1/daily?date=${targetDate}`);
    assert.equal(dailyGet.status, 200);
    const dailyGetJson = (await dailyGet.json()) as { entries: Array<{ sakura_rating: number }> };
    assert.equal(dailyGetJson.entries[0].sakura_rating, 10.71);

    const dailyLb = await app.request(`/api/v1/daily/leaderboard?date=${targetDate}`);
    assert.equal(dailyLb.status, 200);
    const dailyLbJson = (await dailyLb.json()) as { entries: Array<{ sakura_rating: number }> };
    assert.equal(dailyLbJson.entries[0].sakura_rating, 10.71);
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }

  // 8. PostgresCompatibleAdapter getUserSakuraRating stub check
  const pgDir = path.join(process.cwd(), `.sakura-v200-pg-${crypto.randomUUID()}`);
  fs.mkdirSync(pgDir, { recursive: true });
  const pgAdapter = new PostgresCompatibleAdapter("postgres://localhost:5432/sakura", pgDir);
  try {
    const u = pgAdapter.registerUser("PgUser", "hash");
    assert.equal(pgAdapter.getUserSakuraRating(u.id), 0);
  } finally {
    pgAdapter.close();
    fs.rmSync(pgDir, { recursive: true, force: true });
  }
});

test("Sakura Online Server v2.1.0 Dual-Token Sessions, Rotation, Guest Upgrade & Password Change", async () => {
  const tmpDir = path.join(process.cwd(), `.sakura-v210-test-${crypto.randomUUID()}`);
  fs.mkdirSync(tmpDir, { recursive: true });
  const jwtSecret = "v210-unit-test-secret";
  const storage = new SqliteStorageAdapter(tmpDir);
  const app = createApp({ storage, jwtSecret, serverVersion: "v2.1.0" });

  type AuthPayload = {
    ok: boolean;
    token: string;
    refresh_token: string;
    expires_at: number;
    user: {
      id: string;
      username: string;
      is_guest: boolean;
      total_pp: number;
      sakura_rating: number;
      dan_badge: string;
    };
  };

  try {
    const nowSec = Math.floor(Date.now() / 1000);

    // 1. Dual token issuance across /guest, /register, and /login + hashed session storage
    const regRes = await app.request("/api/v1/auth/register", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        username: "Kaguya210",
        password: "initial_pass_123",
        device_name: "Desktop-Main",
      }),
    });
    assert.equal(regRes.status, 201);
    const regData = (await regRes.json()) as AuthPayload;
    assert.equal(regData.ok, true);
    assert.equal(regData.user.is_guest, false);
    assert.ok(regData.refresh_token.length >= 40);
    assert.ok(Math.abs(regData.expires_at - (nowSec + DEFAULT_JWT_TTL_SEC)) <= 10);
    const regJwt = verifyJwt(regData.token, jwtSecret);
    assert.ok(regJwt);
    assert.equal(regJwt.exp, regData.expires_at);
    assert.equal(regJwt.guest, false);

    const rawStored = storage.db
      .prepare("SELECT refresh_token_hash, device_name, expires_at, revoked_at FROM auth_sessions WHERE user_id = ?")
      .get(regData.user.id) as {
      refresh_token_hash: string;
      device_name: string;
      expires_at: number;
      revoked_at: number | null;
    };
    assert.equal(rawStored.refresh_token_hash, hashRefreshToken(regData.refresh_token));
    assert.notEqual(rawStored.refresh_token_hash, regData.refresh_token);
    assert.equal(rawStored.device_name, "Desktop-Main");
    assert.equal(rawStored.revoked_at, null);
    assert.ok(Math.abs(Number(rawStored.expires_at) - (nowSec + REFRESH_TOKEN_TTL_SEC)) <= 10);

    const loginRes = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        username: "Kaguya210",
        password: "initial_pass_123",
        device_name: "Laptop-Sub",
      }),
    });
    assert.equal(loginRes.status, 200);
    const loginData = (await loginRes.json()) as AuthPayload;
    assert.ok(loginData.refresh_token.length >= 40);
    assert.notEqual(loginData.refresh_token, regData.refresh_token);
    assert.ok(Math.abs(loginData.expires_at - (nowSec + DEFAULT_JWT_TTL_SEC)) <= 10);

    // 2. /auth/refresh rotation & replay rejection (401 session_expired) + expired session rejection
    const refreshRes = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: regData.refresh_token, device_name: "Desktop-Main-v2" }),
    });
    assert.equal(refreshRes.status, 200);
    const refreshData = (await refreshRes.json()) as AuthPayload;
    assert.equal(refreshData.ok, true);
    assert.equal(refreshData.user.id, regData.user.id);
    assert.notEqual(refreshData.refresh_token, regData.refresh_token);

    // Replay of rotated refresh_token must be rejected with 401 session_expired
    const replayRes = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: regData.refresh_token }),
    });
    assert.equal(replayRes.status, 401);
    const replayJson = (await replayRes.json()) as { ok: boolean; code: string; error: string };
    assert.equal(replayJson.ok, false);
    assert.equal(replayJson.code, "session_expired");

    // Expired session rejection
    const expToken = "expired_test_token_v210";
    storage.createAuthSession(regData.user.id, hashRefreshToken(expToken), nowSec - 60, "Old-PC");
    const expRes = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: expToken }),
    });
    assert.equal(expRes.status, 401);
    assert.equal(((await expRes.json()) as { code: string }).code, "session_expired");

    // 3. /auth/logout revocation & idempotency
    const logoutRes1 = await app.request("/api/v1/auth/logout", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: refreshData.refresh_token }),
    });
    assert.equal(logoutRes1.status, 200);
    assert.deepEqual(await logoutRes1.json(), { ok: true });

    // Idempotent second logout & empty-body logout
    const logoutRes2 = await app.request("/api/v1/auth/logout", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: refreshData.refresh_token }),
    });
    assert.equal(logoutRes2.status, 200);
    const logoutEmpty = await app.request("/api/v1/auth/logout", { method: "POST" });
    assert.equal(logoutEmpty.status, 200);

    const afterLogoutRefresh = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: refreshData.refresh_token }),
    });
    assert.equal(afterLogoutRefresh.status, 401);
    assert.equal(((await afterLogoutRefresh.json()) as { code: string }).code, "session_expired");

    // 4. /auth/upgrade-guest preserving user.id + scores + PP + Sakura Rating + Dan badge + daily + courses + market uploader_name + 409 conflict
    const guestRes = await app.request("/api/v1/auth/guest", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ nickname: "RisingPetal", device_name: "Guest-PC" }),
    });
    assert.equal(guestRes.status, 201);
    const guestAuth = (await guestRes.json()) as AuthPayload;
    assert.equal(guestAuth.user.is_guest, true);
    const originalGuestId = guestAuth.user.id;
    const originalGuestName = guestAuth.user.username;

    // Guest submits ranked score
    const guestScoreRes = await app.request("/api/v1/scores", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        chart_id: "instant_bloom",
        chart_title: "一瞬花绽",
        chart_hash: "hash_ib_hd_210",
        difficulty: "Hard",
        difficulty_level: 9.0,
        score: 996000,
        accuracy: 99.6,
        max_combo: 400,
        grade: "S",
        perfect_count: 398,
        great_count: 2,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 400,
        synergy_count: 8,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(guestScoreRes.status, 201);
    const guestScoreJson = (await guestScoreRes.json()) as { total_pp: number; sakura_rating: number };
    assert.ok(guestScoreJson.total_pp > 0);
    assert.ok(guestScoreJson.sakura_rating > 0);

    // Guest submits daily score
    const dailyDate = "2026-11-27";
    const dailySpec = computeDailyChallenge(dailyDate);
    const guestDailyRes = await app.request("/api/v1/daily/scores", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        date: dailyDate,
        chart_id: dailySpec.chartId,
        chart_title: dailySpec.chartTitle,
        chart_hash: "hash_daily_210",
        difficulty: dailySpec.difficulty,
        difficulty_level: dailySpec.difficultyLevel,
        score: 988000,
        accuracy: 99.2,
        max_combo: 250,
        grade: "S",
        perfect_count: 247,
        great_count: 3,
        good_count: 0,
        bad_count: 0,
        miss_count: 0,
        total_judgments: 250,
        is_full_combo: true,
        is_all_perfect: false,
        assisted: false,
      }),
    });
    assert.equal(guestDailyRes.status, 201);

    // Guest syncs Dan badge (皆传), achievements, courses, and settings
    const guestSyncRes = await app.request("/api/v1/sync/push", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        scores: [],
        achievements: [{ id: "first_ap", unlocked_at: 1700200000 }],
        course_records: [
          {
            course_id: "dan_kaiden",
            badge_text: "皆传",
            dan_rank: 11,
            total_score: 2990000,
            avg_accuracy: 99.5,
            remaining_life: 80.0,
            cleared: true,
            cleared_at: 1700200100,
          },
        ],
        settings: { note_speed: 7.2 },
        settings_updated_at: 1700200200,
      }),
    });
    assert.equal(guestSyncRes.status, 200);

    // Guest uploads a market chart and rates it
    const pkgInfo = JSON.stringify({
      id: "guest_legacy_chart",
      title: "Guest Legacy Chart",
      artist: "Sakura Artist",
      charter: "RisingPetal",
      bpm: 180,
      audio: "track.wav",
      tags: ["sakura"],
      difficulties: [{ name: "Normal", level: 5.0, chart_file: "normal.json" }],
    });
    const pkgChart = JSON.stringify({
      offset: 0,
      bpm: 180,
      keyboard_notes: [{ lane: 0, time: 500, duration: 0 }],
      mouse_notes: [],
    });
    const pkgZip = createStoreZipArchive({
      "info.json": pkgInfo,
      "normal.json": pkgChart,
      "track.wav": "RIFF____WAVEfmt ",
    });
    const chartUpRes = await app.request("/api/v1/charts", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/octet-stream",
      },
      body: new Uint8Array(pkgZip),
    });
    assert.equal(chartUpRes.status, 201);
    const chartUpJson = (await chartUpRes.json()) as { chart: { id: string; uploader_name: string } };
    assert.equal(chartUpJson.chart.uploader_name, originalGuestName);

    const rateChartRes = await app.request("/api/v1/charts/guest_legacy_chart/rate", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ rating: 5, comment: "My debut chart!" }),
    });
    assert.equal(rateChartRes.status, 200);

    const guestMeBefore = await app.request("/api/v1/auth/me", {
      headers: { Authorization: `Bearer ${guestAuth.token}` },
    });
    const preUpgradeUser = ((await guestMeBefore.json()) as { user: AuthPayload["user"] }).user;
    assert.equal(preUpgradeUser.dan_badge, "皆传");

    // Reject non-guest calling /upgrade-guest (400)
    const nonGuestUpgrade = await app.request("/api/v1/auth/upgrade-guest", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${loginData.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ username: "AnotherName", password: "valid_pass_123" }),
    });
    assert.equal(nonGuestUpgrade.status, 400);

    // Reject invalid username / password (400)
    const badNameUpgrade = await app.request("/api/v1/auth/upgrade-guest", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ username: "x", password: "valid_pass_123" }),
    });
    assert.equal(badNameUpgrade.status, 400);

    const badPassUpgrade = await app.request("/api/v1/auth/upgrade-guest", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ username: "SakuraBloom", password: "12345" }),
    });
    assert.equal(badPassUpgrade.status, 400);

    // Reject case-insensitive username conflict (409)
    const conflictUpgrade = await app.request("/api/v1/auth/upgrade-guest", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ username: "kaguya210", password: "valid_pass_123" }),
    });
    assert.equal(conflictUpgrade.status, 409);

    // Valid in-place guest upgrade (200)
    const upgradeRes = await app.request("/api/v1/auth/upgrade-guest", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${guestAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        username: "SakuraBloom",
        password: "bloom_pass_210",
        refresh_token: guestAuth.refresh_token,
        device_name: "Guest-PC-Upgraded",
      }),
    });
    assert.equal(upgradeRes.status, 200);
    const upgradedAuth = (await upgradeRes.json()) as AuthPayload;
    assert.equal(upgradedAuth.ok, true);
    assert.equal(upgradedAuth.user.id, originalGuestId);
    assert.equal(upgradedAuth.user.username, "SakuraBloom");
    assert.equal(upgradedAuth.user.is_guest, false);
    assert.equal(upgradedAuth.user.total_pp, preUpgradeUser.total_pp);
    assert.equal(upgradedAuth.user.sakura_rating, preUpgradeUser.sakura_rating);
    assert.equal(upgradedAuth.user.dan_badge, "皆传");
    assert.equal(verifyJwt(upgradedAuth.token, jwtSecret)?.guest, false);

    // Verify ranked leaderboard, daily leaderboard, cloud sync, and market chart uploader_name all preserved & updated
    const lbCheck = await app.request("/api/v1/leaderboards/instant_bloom?difficulty=Hard", {
      headers: { Authorization: `Bearer ${upgradedAuth.token}` },
    });
    const lbCheckJson = (await lbCheck.json()) as {
      my_rank: number;
      entries: Array<{ user_id: string; username: string; dan_badge: string; pp: number; sakura_rating: number }>;
    };
    assert.equal(lbCheckJson.my_rank, 1);
    assert.equal(lbCheckJson.entries[0].user_id, originalGuestId);
    assert.equal(lbCheckJson.entries[0].username, "SakuraBloom");
    assert.equal(lbCheckJson.entries[0].dan_badge, "皆传");
    assert.equal(lbCheckJson.entries[0].sakura_rating, preUpgradeUser.sakura_rating);

    const dailyCheck = await app.request(`/api/v1/daily/leaderboard?date=${dailyDate}`, {
      headers: { Authorization: `Bearer ${upgradedAuth.token}` },
    });
    const dailyCheckJson = (await dailyCheck.json()) as {
      my_rank: number;
      entries: Array<{ user_id: string; username: string }>;
    };
    assert.equal(dailyCheckJson.my_rank, 1);
    assert.equal(dailyCheckJson.entries[0].user_id, originalGuestId);
    assert.equal(dailyCheckJson.entries[0].username, "SakuraBloom");

    const syncCheck = await app.request("/api/v1/sync/pull", {
      headers: { Authorization: `Bearer ${upgradedAuth.token}` },
    });
    const syncCheckJson = (await syncCheck.json()) as {
      scores: unknown[];
      achievements: Array<{ id: string }>;
      course_records: Array<{ course_id: string; badge_text: string; cleared: boolean }>;
      settings: { note_speed: number };
    };
    assert.equal(syncCheckJson.scores.length, 1);
    assert.equal(syncCheckJson.achievements.length, 1);
    assert.equal(syncCheckJson.course_records[0].badge_text, "皆传");
    assert.equal(syncCheckJson.settings.note_speed, 7.2);

    const chartCheck = await app.request("/api/v1/charts/guest_legacy_chart", {
      headers: { Authorization: `Bearer ${upgradedAuth.token}` },
    });
    const chartCheckJson = (await chartCheck.json()) as {
      chart: { uploader_id: string; uploader_name: string; my_rating: number };
      ratings: Array<{ user_id: string; username: string; rating: number }>;
    };
    assert.equal(chartCheckJson.chart.uploader_id, originalGuestId);
    assert.equal(chartCheckJson.chart.uploader_name, "SakuraBloom");
    assert.equal(chartCheckJson.chart.my_rating, 5);
    assert.equal(chartCheckJson.ratings[0].user_id, originalGuestId);
    assert.equal(chartCheckJson.ratings[0].username, "SakuraBloom");

    // 5. /auth/change-password verification, password update, cross-device session revocation, and new dual-token issuance
    const secondGuestRes = await app.request("/api/v1/auth/guest", { method: "POST" });
    const secondGuest = (await secondGuestRes.json()) as AuthPayload;
    const guestPwAttempt = await app.request("/api/v1/auth/change-password", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${secondGuest.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ old_password: "", new_password: "new_secret_123" }),
    });
    assert.equal(guestPwAttempt.status, 400);

    // Create a second active session on SakuraBloom
    const bloomLogin2 = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraBloom", password: "bloom_pass_210", device_name: "Tablet" }),
    });
    assert.equal(bloomLogin2.status, 200);
    const bloomSession2 = (await bloomLogin2.json()) as AuthPayload;

    // Wrong old_password -> 401
    const wrongOldPw = await app.request("/api/v1/auth/change-password", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${upgradedAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ old_password: "wrong_password", new_password: "sakura_new_pw_777" }),
    });
    assert.equal(wrongOldPw.status, 401);

    // Valid password change -> 200 + revokes all prior sessions + issues fresh tokens
    const changePwRes = await app.request("/api/v1/auth/change-password", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${upgradedAuth.token}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        old_password: "bloom_pass_210",
        new_password: "sakura_new_pw_777",
        refresh_token: upgradedAuth.refresh_token,
        device_name: "Guest-PC-Upgraded",
      }),
    });
    assert.equal(changePwRes.status, 200);
    const changedAuth = (await changePwRes.json()) as AuthPayload;
    assert.equal(changedAuth.ok, true);
    assert.equal(changedAuth.user.id, originalGuestId);
    assert.notEqual(changedAuth.refresh_token, upgradedAuth.refresh_token);

    // Old password rejected, new password accepted
    const oldPwLogin = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraBloom", password: "bloom_pass_210" }),
    });
    assert.equal(oldPwLogin.status, 401);

    const newPwLogin = await app.request("/api/v1/auth/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: "SakuraBloom", password: "sakura_new_pw_777" }),
    });
    assert.equal(newPwLogin.status, 200);

    // Both prior refresh tokens are now revoked
    const priorRefresh1 = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: upgradedAuth.refresh_token }),
    });
    assert.equal(priorRefresh1.status, 401);

    const priorRefresh2 = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: bloomSession2.refresh_token }),
    });
    assert.equal(priorRefresh2.status, 401);

    // Newly issued refresh_token from /auth/change-password works
    const freshRefresh = await app.request("/api/v1/auth/refresh", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh_token: changedAuth.refresh_token }),
    });
    assert.equal(freshRefresh.status, 200);
  } finally {
    storage.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
});
