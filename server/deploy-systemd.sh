#!/usr/bin/env bash
# Sakura Online Backend deployment helper for Ubuntu 24.04 (/srv/sakura-online)
set -euo pipefail

APP_DIR="${APP_DIR:-/srv/sakura-online}"
NODE_BIN="${NODE_BIN:-/opt/node-v22/bin}"
export PATH="${NODE_BIN}:${PATH}"

cd "${APP_DIR}"
npm install --no-audit --no-fund
npm run build
sudo systemctl restart sakura-online.service
sleep 1
systemctl is-active --quiet sakura-online.service
echo "[OK] sakura-online.service restarted and healthy."
