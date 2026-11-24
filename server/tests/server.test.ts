import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createApp } from "../src/app.js";
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
