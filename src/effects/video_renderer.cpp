// video_renderer.cpp — 动态视频舞台渲染与流式 GPU 纹理更新实现

#include "video_renderer.h"

#include <algorithm>

namespace sakura::effects
{

bool VideoRenderer::Open(std::string_view videoPath, bool loop)
{
    Close();
    m_lastUploadedSeq  = 0;
    m_hasPendingUpload = false;
    m_hasUploadedFrame = false;
    if (!m_decoder.Open(videoPath, loop))
    {
        VideoDecoder::PublishGlobalStageMode(VideoStageMode::Fallback);
        return false;
    }
    VideoDecoder::PublishGlobalStageMode(VideoStageMode::HardwareDecode);
    return true;
}

void VideoRenderer::Close()
{
    m_decoder.Close();
    if (m_streamTex)
    {
        SDL_DestroyTexture(m_streamTex);
        m_streamTex = nullptr;
    }
    m_texW = 0;
    m_texH = 0;
    m_lastUploadedSeq  = 0;
    m_hasPendingUpload = false;
    m_hasUploadedFrame = false;
    VideoDecoder::PublishGlobalStageMode(VideoStageMode::Idle);
}

void VideoRenderer::Update(double playbackSeconds, bool bgaEnabled)
{
    m_bgaEnabled = bgaEnabled;
    if (!bgaEnabled)
    {
        VideoDecoder::PublishGlobalStageMode(VideoStageMode::Disabled);
        return;
    }
    if (!m_decoder.IsOpen() || m_decoder.IsFallbackActive())
    {
        VideoDecoder::PublishGlobalStageMode(VideoStageMode::Fallback);
        return;
    }

    if (m_decoder.AcquireFrame(std::max(0.0, playbackSeconds), m_stagingFrame, m_lastUploadedSeq))
    {
        m_hasPendingUpload = true;
    }
    VideoDecoder::PublishGlobalStageMode(
        m_decoder.IsFallbackActive() ? VideoStageMode::Fallback : VideoStageMode::HardwareDecode);
}

bool VideoRenderer::Render(sakura::core::Renderer& renderer, float stageDimming)
{
    if (!m_bgaEnabled || m_decoder.IsFallbackActive() || !m_decoder.IsOpen())
        return false;

    SDL_Renderer* native = renderer.GetSDLRenderer();
    if (!native)
        return false;

    if (m_hasPendingUpload && m_stagingFrame.width > 0 && m_stagingFrame.height > 0 && !m_stagingFrame.rgba.empty())
    {
        if (!m_streamTex || m_texW != m_stagingFrame.width || m_texH != m_stagingFrame.height)
        {
            if (m_streamTex) SDL_DestroyTexture(m_streamTex);
            m_streamTex = SDL_CreateTexture(
                native, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                m_stagingFrame.width, m_stagingFrame.height);
            if (m_streamTex)
            {
                SDL_SetTextureScaleMode(m_streamTex, SDL_SCALEMODE_LINEAR);
                SDL_SetTextureBlendMode(m_streamTex, SDL_BLENDMODE_NONE);
            }
            m_texW = m_stagingFrame.width;
            m_texH = m_stagingFrame.height;
        }
        if (m_streamTex)
        {
            SDL_UpdateTexture(m_streamTex, nullptr, m_stagingFrame.rgba.data(), m_stagingFrame.width * 4);
            m_hasUploadedFrame = true;
        }
        m_hasPendingUpload = false;
    }

    if (!m_streamTex || !m_hasUploadedFrame)
        return false;

    const int sw = std::max(1, renderer.GetScreenWidth());
    const int sh = std::max(1, renderer.GetScreenHeight());
    const float vw = static_cast<float>(std::max(1, m_texW));
    const float vh = static_cast<float>(std::max(1, m_texH));
    const float scale = std::max(static_cast<float>(sw) / vw, static_cast<float>(sh) / vh);
    const float cropW = static_cast<float>(sw) / scale;
    const float cropH = static_cast<float>(sh) / scale;
    const SDL_FRect srcCrop{
        (vw - cropW) * 0.5f,
        (vh - cropH) * 0.5f,
        cropW,
        cropH
    };
    SDL_RenderTexture(native, m_streamTex, &srcCrop, nullptr);

    const float dim = std::clamp(stageDimming, 0.20f, 0.90f);
    const uint8_t topA = static_cast<uint8_t>(std::clamp(dim * 242.0f, 0.0f, 255.0f));
    const uint8_t midA = static_cast<uint8_t>(std::clamp(dim * 212.0f, 0.0f, 255.0f));
    renderer.DrawGradientRect(
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 8, 10, 22, topA }, { 12, 12, 28, midA },
        { 6, 8, 18, topA },  { 10, 12, 24, topA });
    return true;
}

} // namespace sakura::effects