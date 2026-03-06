#!/usr/bin/env bash
# .devcontainer/post-create.sh
# 容器创建后自动运行：安装 vcpkg 依赖并预配置 CMake

set -euo pipefail

echo "=== [post-create] 初始化 Sakura 开发环境 ==="

# 确保 vcpkg 二进制缓存目录存在
mkdir -p "${VCPKG_DEFAULT_BINARY_CACHE:-/root/.cache/vcpkg/archives}"

# 安装 vcpkg manifest 所有依赖（含 triplet x64-linux）
echo "--- 安装 vcpkg 依赖（首次可能需要较长时间）---"
vcpkg install \
    --triplet x64-linux \
    --host-triplet x64-linux \
    --x-manifest-root="$(pwd)" \
    --x-install-root="$(pwd)/build/ci-linux/vcpkg_installed" \
    --no-print-usage

# 预跑 CMake configure 生成 compile_commands.json（IntelliSense 使用）
echo "--- 运行 CMake configure (ci-linux) ---"
cmake --preset ci-linux -DSAKURA_BUILD_TESTS=ON || true

echo "=== [post-create] 完成 ==="
