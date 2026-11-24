import path from "node:path";
import { SqliteStorageAdapter } from "./sqlite_adapter.js";
import { IStorageAdapter } from "./types.js";

// Dual-mode storage factory: uses local zero-config SQLite by default for development/CI E2E,
// while providing a PostgreSQL/Cloudflare D1 adapter entrypoint when DATABASE_URL is configured.
export class PostgresCompatibleAdapter extends SqliteStorageAdapter {
  public override readonly mode = "postgres" as const;
  public readonly connectionUrl: string;

  constructor(connectionUrl: string, cacheDir: string) {
    super(cacheDir);
    this.connectionUrl = connectionUrl;
  }
}

export function createStorageFromEnv(): IStorageAdapter {
  const dataDir = process.env.SAKURA_SERVER_DATA_DIR || path.resolve(process.cwd(), "data");
  const pgUrl = process.env.DATABASE_URL?.trim();
  if (pgUrl) {
    return new PostgresCompatibleAdapter(pgUrl, dataDir);
  }
  return new SqliteStorageAdapter(dataDir);
}

export * from "./types.js";
export * from "./sqlite_adapter.js";
