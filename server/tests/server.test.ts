import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createApp } from "../src/app.js";
import { PostgresCompatibleAdapter, SqliteStorageAdapter } from "../src/storage/index.js";

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
