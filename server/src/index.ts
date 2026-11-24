import { serve } from "@hono/node-server";
import { createApp } from "./app.js";
import { createStorageFromEnv } from "./storage/index.js";

const port = Number(process.env.PORT || "233");
const jwtSecret = process.env.SAKURA_JWT_SECRET || "sakura-local-dev-jwt-secret";
const storage = createStorageFromEnv();
const app = createApp({ storage, jwtSecret });

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
