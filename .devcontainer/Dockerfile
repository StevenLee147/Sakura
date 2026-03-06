FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# ─── 系统依赖：SDL3 vcpkg 构建所需的系统头文件 ──────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    # 构建工具
    ninja-build \
    pkg-config \
    # SDL3 on Linux 所需的 X11 / GL / input 头文件
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxi-dev \
    libxfixes-dev \
    libxcursor-dev \
    libxxf86vm-dev \
    libxss-dev \
    libgl-dev \
    libgles2-mesa-dev \
    libegl-dev \
    # Wayland (SDL3 fallback)
    libwayland-dev \
    libxkbcommon-dev \
    # 音频
    libpulse-dev \
    libasound2-dev \
    # udev (SDL3 gamepad/joystick)
    libudev-dev \
    # SDL3_image 依赖
    libpng-dev \
    libjpeg-turbo8-dev \
    libwebp-dev \
    # SDL3_ttf 依赖
    libfreetype-dev \
    libharfbuzz-dev \
    # 杂项
    curl \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

# ─── 安装 vcpkg ──────────────────────────────────────────────────────────────
# 使用与 vcpkg.json builtin-baseline 对应的提交
ARG VCPKG_COMMIT=ffc071e0c08432c60c9b64f00334c0227667931b
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && cd /opt/vcpkg \
    && git checkout ${VCPKG_COMMIT} \
    && ./bootstrap-vcpkg.sh -disableMetrics \
    && ln -s /opt/vcpkg/vcpkg /usr/local/bin/vcpkg

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"
