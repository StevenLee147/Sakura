import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import test from "node:test";
import { computeDailyChallenge, createApp } from "../src/app.js";
import { PostgresCompatibleAdapter, SqliteStorageAdapter } from "../src/storage/index.js";
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
