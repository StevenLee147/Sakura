#pragma once

// video_renderer.h — 动态视频舞台纹理流式上传与无缝回退渲染器

#include "video_decoder.h"
#include "core/renderer.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <string_view>

namespace sakura::effects
{

class VideoRenderer
{
public:
    VideoRenderer() = default;
    ~VideoRenderer() { Close(); }

    VideoRenderer(const VideoRenderer&)            = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    bool Open(std::string_view videoPath, bool loop = true);
    void Close();

    void Update(double playbackSeconds, bool bgaEnabled);
    bool Render(sakura::core::Renderer& renderer, float stageDimming);

    bool IsActive() const { return m_hasUploadedFrame && !m_decoder.IsFallbackActive(); }
    bool IsFallback() const { return m_decoder.IsFallbackActive(); }

private:
    VideoDecoder m_decoder;
    VideoFrame   m_stagingFrame;
    SDL_Texture* m_streamTex        = nullptr;
    int          m_texW             = 0;
    int          m_texH             = 0;
    uint64_t     m_lastUploadedSeq  = 0;
    bool         m_hasPendingUpload = false;
    bool         m_hasUploadedFrame = false;
    bool         m_bgaEnabled       = true;
};

} // namespace sakura::effects