import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { serve } from "@hono/node-server";
import { createApp } from "./app.js";
import { createStorageFromEnv } from "./storage/index.js";

function readPackageVersion(): string | undefined {
  try {
    const pkgPath = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../package.json");
    const pkg = JSON.parse(fs.readFileSync(pkgPath, "utf8")) as { version?: string };
    if (pkg.version) {
      return pkg.version.startsWith("v") ? pkg.version : "v" + pkg.version;
    }
  } catch {}
  return undefined;
}

const port = Number(process.env.PORT || "233");
const jwtSecret = process.env.SAKURA_JWT_SECRET || "sakura-local-dev-jwt-secret";
const storage = createStorageFromEnv();
const app = createApp({ storage, jwtSecret, serverVersion: readPackageVersion() });

const server = serve(
  {
    fetch: app.fetch,
    port,
    hostname: process.env.HOST || "0.0.0.0",
  },
  (info) => {
    console.log(`[sakura-server] listening on http://${info.address}:${info.port} (mode=${storage.mode})`);
  }
);

const shutdown = () => {
  server.close(() => {
    storage.close();
    process.exit(0);
  });
};

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
