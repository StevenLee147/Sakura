#include "renderer.h"
#include "utils/logger.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sakura::core
{

// ============================================================================
// 预制颜色定义
// ============================================================================
const Color Color::White       = { 255, 255, 255, 255 };
const Color Color::Black       = {   0,   0,   0, 255 };
const Color Color::Red         = { 255,   0,   0, 255 };
const Color Color::Green       = {   0, 255,   0, 255 };
const Color Color::Blue        = {   0,   0, 255, 255 };
const Color Color::Yellow      = { 255, 255,   0, 255 };
const Color Color::Cyan        = {   0, 255, 255, 255 };
const Color Color::Magenta     = { 255,   0, 255, 255 };
const Color Color::Transparent = {   0,   0,   0,   0 };
const Color Color::DarkBlue    = {  15,  15,  35, 255 };

// ============================================================================
// Renderer 实现
// ============================================================================

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    Destroy();
}

bool Renderer::Initialize(SDL_Window* window)
{
    if (!window)
    {
        LOG_ERROR("Renderer::Initialize: window 为 nullptr");
        return false;
    }

    m_window = window;

    // 优先尝试 GPU 后端（SDL3.4+）
    m_renderer = SDL_CreateRenderer(window, "gpu");
    if (!m_renderer)
    {
        // 退回到系统默认后端
        LOG_WARN("GPU 渲染器不可用 ({}), 使用默认后端", SDL_GetError());
        m_renderer = SDL_CreateRenderer(window, nullptr);
    }

    if (!m_renderer)
    {
        LOG_ERROR("SDL_CreateRenderer 失败: {}", SDL_GetError());
        return false;
    }

    // 启用 Alpha 混合
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    LOG_INFO("渲染器初始化成功，后端: {}", SDL_GetRendererName(m_renderer));
    return true;
}

void Renderer::Destroy()
{
    if (m_renderer)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
        LOG_DEBUG("渲染器已销毁");
    }
    m_window = nullptr;
}

void Renderer::BeginFrame()
{
    // SDL_Renderer 无需显式 BeginFrame，但我们保留此接口供扩展
}

void Renderer::EndFrame()
{
    // 渲染结束：重置 viewport 再提交
    SDL_SetRenderViewport(m_renderer, nullptr);
    SDL_RenderPresent(m_renderer);
}

void Renderer::SetViewportShake(int pixelDx, int pixelDy)
{
    m_shakeOffsetX = pixelDx;
    m_shakeOffsetY = pixelDy;
}

void Renderer::ResetViewportShake()
{
    m_shakeOffsetX = 0;
    m_shakeOffsetY = 0;
}

void Renderer::Clear(Color color)
{
    // 先重置 viewport 填充全屏背景
    SDL_SetRenderViewport(m_renderer, nullptr);
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(m_renderer);

    // 若有震动偏移，设置 viewport 使后续绘制产生位移
    if (m_shakeOffsetX != 0 || m_shakeOffsetY != 0)
    {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        SDL_Rect vp = {
            m_shakeOffsetX,
            m_shakeOffsetY,
            sw,
            sh
        };
        SDL_SetRenderViewport(m_renderer, &vp);
    }
}

// ── 归一化坐标转换 ────────────────────────────────────────────────────────────

float Renderer::ToPixelX(float normX) const
{
    return normX * static_cast<float>(GetScreenWidth());
}

float Renderer::ToPixelY(float normY) const
{
    return normY * static_cast<float>(GetScreenHeight());
}

float Renderer::ToPixelW(float normW) const
{
    return normW * static_cast<float>(GetScreenWidth());
}

float Renderer::ToPixelH(float normH) const
{
    return normH * static_cast<float>(GetScreenHeight());
}

// ── 基础图形 ──────────────────────────────────────────────────────────────────

void Renderer::DrawFilledRect(NormRect rect, Color color)
{
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_FRect pixelRect = rect.ToPixel(GetScreenWidth(), GetScreenHeight());
    SDL_RenderFillRect(m_renderer, &pixelRect);
}

void Renderer::DrawRectOutline(NormRect rect, Color color, float normThickness)
{
    // 将外框拆解为四个填充矩形（上/下/左/右边框）
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float pxX = rect.x      * sw;
    float pxY = rect.y      * sh;
    float pxW = rect.width  * sw;
    float pxH = rect.height * sh;
    float t   = normThickness * std::min(sw, sh);  // 边框粗细（像素）

    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

    // 上边
    SDL_FRect top    = { pxX,           pxY,           pxW,  t  };
    // 下边
    SDL_FRect bottom = { pxX,           pxY + pxH - t, pxW,  t  };
    // 左边
    SDL_FRect left   = { pxX,           pxY + t,       t,    pxH - 2*t };
    // 右边
    SDL_FRect right  = { pxX + pxW - t, pxY + t,       t,    pxH - 2*t };

    SDL_RenderFillRect(m_renderer, &top);
    SDL_RenderFillRect(m_renderer, &bottom);
    SDL_RenderFillRect(m_renderer, &left);
    SDL_RenderFillRect(m_renderer, &right);
}

// ── 混合模式 ──────────────────────────────────────────────────────────────────

void Renderer::SetBlendMode(BlendMode mode)
{
    SDL_BlendMode sdlMode = SDL_BLENDMODE_NONE;
    switch (mode)
    {
        case BlendMode::None:      sdlMode = SDL_BLENDMODE_NONE;  break;
        case BlendMode::Alpha:     sdlMode = SDL_BLENDMODE_BLEND; break;
        case BlendMode::Additive:  sdlMode = SDL_BLENDMODE_ADD;   break;
        case BlendMode::Multiply:  sdlMode = SDL_BLENDMODE_MUL;   break;
    }
    SDL_SetRenderDrawBlendMode(m_renderer, sdlMode);
}

// ── 屏幕信息 ──────────────────────────────────────────────────────────────────

int Renderer::GetScreenWidth() const
{
    int w = 0, h = 0;
    if (m_renderer)
    {
        SDL_GetCurrentRenderOutputSize(m_renderer, &w, &h);
    }
    return w;
}

int Renderer::GetScreenHeight() const
{
    int w = 0, h = 0;
    if (m_renderer)
    {
        SDL_GetCurrentRenderOutputSize(m_renderer, &w, &h);
    }
    return h;
}

SDL_GPUDevice* Renderer::GetGPUDevice() const
{
    if (!m_renderer) return nullptr;
    return SDL_GetGPURendererDevice(m_renderer);
}

// ── 文字渲染 ──────────────────────────────────────────────────────────────────

void Renderer::DrawText(FontHandle fontHandle,
                        std::string_view text,
                        float normX,
                        float normY,
                        float normFontSize,
                        Color color,
                        TextAlign align)
{
    if (!m_renderer || text.empty()) return;

    TTF_Font* font = ResourceManager::GetInstance().GetFont(fontHandle);
    if (!font)
    {
        LOG_WARN("Renderer::DrawText: 无效 FontHandle {}", fontHandle);
        return;
    }

    const int screenH = GetScreenHeight();
    const int screenW = GetScreenWidth();

    // 目标像素字号（相对屏幕高度）
    const float targetPixelSize = normFontSize * static_cast<float>(screenH);

    // 暂时改字号（记录原始值以便还原）
    const float originalSize = TTF_GetFontSize(font);
    if (std::abs(targetPixelSize - originalSize) > 0.5f)
    {
        TTF_SetFontSize(font, targetPixelSize);
    }

    // 渲染到 SDL_Surface（支持 UTF-8）
    SDL_Color sdlColor = { color.r, color.g, color.b, color.a };
    std::string textStr(text);
    SDL_Surface* surface = TTF_RenderText_Blended(font, textStr.c_str(), 0, sdlColor);

    // 还原字号
    if (std::abs(targetPixelSize - originalSize) > 0.5f)
    {
        TTF_SetFontSize(font, originalSize);
    }

    if (!surface)
    {
        LOG_WARN("TTF_RenderText_Blended 失败: {}", SDL_GetError());
        return;
    }

    // Surface → GPU Texture
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture)
    {
        LOG_WARN("SDL_CreateTextureFromSurface 失败: {}", SDL_GetError());
        return;
    }

    // 获取纹理尺寸
    float texW = 0.0f, texH = 0.0f;
    SDL_GetTextureSize(texture, &texW, &texH);

    // 根据对齐方式计算左上角像素坐标
    float pxX = normX * static_cast<float>(screenW);
    float pxY = normY * static_cast<float>(screenH);

    switch (align)
    {
        case TextAlign::Center:
            pxX -= texW * 0.5f;
            break;
        case TextAlign::Right:
            pxX -= texW;
            break;
        case TextAlign::Left:
        default:
            break;
    }

    SDL_FRect dest = { pxX, pxY, texW, texH };
    SDL_SetTextureAlphaMod(texture, color.a);
    SDL_RenderTexture(m_renderer, texture, nullptr, &dest);
    SDL_DestroyTexture(texture);
}

float Renderer::MeasureTextWidth(FontHandle fontHandle,
                                  std::string_view text,
                                  float normFontSize) const
{
    if (!m_renderer || text.empty()) return 0.0f;

    TTF_Font* font = ResourceManager::GetInstance().GetFont(fontHandle);
    if (!font) return 0.0f;

    const float targetPixelSize = normFontSize * static_cast<float>(GetScreenHeight());
    const float originalSize    = TTF_GetFontSize(font);

    if (std::abs(targetPixelSize - originalSize) > 0.5f)
    {
        TTF_SetFontSize(font, targetPixelSize);
    }

    int w = 0, h = 0;
    std::string textStr(text);
    TTF_GetStringSize(font, textStr.c_str(), 0, &w, &h);

    if (std::abs(targetPixelSize - originalSize) > 0.5f)
    {
        TTF_SetFontSize(font, originalSize);
    }

    return static_cast<float>(w) / static_cast<float>(GetScreenWidth());
}

// ============================================================================
// Sprite 渲染
// ============================================================================

void Renderer::DrawSprite(TextureHandle texHandle,
                           NormRect dest,
                           float rotation,
                           Color tint,
                           float alpha)
{
    SDL_Texture* tex = ResourceManager::GetInstance().GetTexture(texHandle);
    if (!tex || !m_renderer) return;

    SDL_FRect dstRect = dest.ToPixel(GetScreenWidth(), GetScreenHeight());
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, static_cast<uint8_t>(alpha * 255.0f));

    if (rotation == 0.0f)
    {
        SDL_RenderTexture(m_renderer, tex, nullptr, &dstRect);
    }
    else
    {
        SDL_RenderTextureRotated(m_renderer, tex, nullptr, &dstRect,
                                 static_cast<double>(rotation),
                                 nullptr,
                                 SDL_FLIP_NONE);
    }
    // 还原
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

void Renderer::DrawSpriteEx(TextureHandle texHandle,
                             NormRect src,
                             NormRect dest,
                             float rotation,
                             Color tint,
                             float alpha)
{
    SDL_Texture* tex = ResourceManager::GetInstance().GetTexture(texHandle);
    if (!tex || !m_renderer) return;

    float texW = 0.0f, texH = 0.0f;
    SDL_GetTextureSize(tex, &texW, &texH);

    SDL_FRect srcRect = {
        src.x     * texW,
        src.y     * texH,
        src.width  * texW,
        src.height * texH
    };
    SDL_FRect dstRect = dest.ToPixel(GetScreenWidth(), GetScreenHeight());

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, static_cast<uint8_t>(alpha * 255.0f));

    if (rotation == 0.0f)
    {
        SDL_RenderTexture(m_renderer, tex, &srcRect, &dstRect);
    }
    else
    {
        SDL_RenderTextureRotated(m_renderer, tex, &srcRect, &dstRect,
                                 static_cast<double>(rotation),
                                 nullptr,
                                 SDL_FLIP_NONE);
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

// ============================================================================
// 几何图形
// ============================================================================

static constexpr float kPi = 3.14159265358979323846f;

// 内部工具：构建三角扇形顶点（圆心 + 若干边缘顶点）
static void BuildCircleGeometry(
    float pxCX, float pxCY, float pxRadius,
    int segments,
    float angleStartRad, float angleEndRad,
    const SDL_FColor& fcolor,
    std::vector<SDL_Vertex>& verts,
    std::vector<int>& indices)
{
    const float step = (angleEndRad - angleStartRad) / static_cast<float>(segments);

    const size_t centerIdx = verts.size();
    verts.push_back({ { pxCX, pxCY }, fcolor, { 0.5f, 0.5f } });

    for (int i = 0; i <= segments; ++i)
    {
        const float angle = angleStartRad + step * static_cast<float>(i);
        verts.push_back({
            { pxCX + std::cos(angle) * pxRadius,
              pxCY + std::sin(angle) * pxRadius },
            fcolor,
            { 0.5f + 0.5f * std::cos(angle),
              0.5f + 0.5f * std::sin(angle) }
        });

        if (i > 0)
        {
            indices.push_back(static_cast<int>(centerIdx));
            indices.push_back(static_cast<int>(centerIdx) + i);
            indices.push_back(static_cast<int>(centerIdx) + i + 1);
        }
    }
}

void Renderer::DrawCircleFilled(float cx, float cy, float normRadius,
                                 Color color, int segments)
{
    if (!m_renderer) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const float pxCX = cx * sw;
    const float pxCY = cy * sh;
    const float pxR  = normRadius * std::min(sw, sh);

    const SDL_FColor fc = color.ToSDLFColor();

    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    verts.reserve(static_cast<size_t>(segments + 2));
    indices.reserve(static_cast<size_t>(segments * 3));

    BuildCircleGeometry(pxCX, pxCY, pxR, segments, 0.0f, kPi * 2.0f, fc, verts, indices);

    SDL_RenderGeometry(m_renderer, nullptr,
        verts.data(), static_cast<int>(verts.size()),
        indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawCircleOutline(float cx, float cy, float normRadius,
                                  Color color, float normThickness, int segments)
{
    if (!m_renderer) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const float pxCX    = cx * sw;
    const float pxCY    = cy * sh;
    const float pxR     = normRadius * std::min(sw, sh);
    const float pxThick = normThickness * std::min(sw, sh);

    const SDL_FColor fc = color.ToSDLFColor();
    const float outerR = pxR + pxThick * 0.5f;
    const float innerR = pxR - pxThick * 0.5f;

    // 环形：外圈 & 内圈顶点，每段两个三角形
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    const int n = segments;
    verts.reserve(static_cast<size_t>((n + 1) * 2));
    indices.reserve(static_cast<size_t>(n * 6));

    for (int i = 0; i <= n; ++i)
    {
        const float angle = kPi * 2.0f * static_cast<float>(i) / static_cast<float>(n);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        verts.push_back({ { pxCX + cosA * outerR, pxCY + sinA * outerR }, fc, { 0,0 } });
        verts.push_back({ { pxCX + cosA * innerR, pxCY + sinA * innerR }, fc, { 0,0 } });

        if (i > 0)
        {
            int bo = (i - 1) * 2;
            indices.push_back(bo);     indices.push_back(bo + 1); indices.push_back(bo + 2);
            indices.push_back(bo + 1); indices.push_back(bo + 3); indices.push_back(bo + 2);
        }
    }

    SDL_RenderGeometry(m_renderer, nullptr,
        verts.data(), static_cast<int>(verts.size()),
        indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2,
                         Color color, float normThickness)
{
    if (!m_renderer) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    const float px1 = x1 * sw, py1 = y1 * sh;
    const float px2 = x2 * sw, py2 = y2 * sh;
    const float halfT = normThickness * 0.5f * std::min(sw, sh);

    // 方向向量
    const float dx = px2 - px1;
    const float dy = py2 - py1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    // 垂直方向（归一化 * halfT）
    const float nx = -dy / len * halfT;
    const float ny =  dx / len * halfT;

    const SDL_FColor fc = color.ToSDLFColor();

    SDL_Vertex verts[4] = {
        { { px1 + nx, py1 + ny }, fc, { 0,0 } },
        { { px1 - nx, py1 - ny }, fc, { 0,0 } },
        { { px2 + nx, py2 + ny }, fc, { 0,0 } },
        { { px2 - nx, py2 - ny }, fc, { 0,0 } }
    };
    int indices[] = { 0, 1, 2, 1, 3, 2 };

    SDL_RenderGeometry(m_renderer, nullptr, verts, 4, indices, 6);
}

void Renderer::DrawArc(float cx, float cy, float normRadius,
                        float startAngleDeg, float endAngleDeg,
                        Color color, float normThickness, int segments)
{
    if (!m_renderer) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const float pxCX    = cx * sw;
    const float pxCY    = cy * sh;
    const float pxR     = normRadius * std::min(sw, sh);
    const float pxThick = normThickness * std::min(sw, sh);

    const float startRad = startAngleDeg * kPi / 180.0f;
    const float endRad   = endAngleDeg   * kPi / 180.0f;
    const float outerR = pxR + pxThick * 0.5f;
    const float innerR = pxR - pxThick * 0.5f;

    const SDL_FColor fc = color.ToSDLFColor();

    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;
    verts.reserve(static_cast<size_t>((segments + 1) * 2));
    indices.reserve(static_cast<size_t>(segments * 6));

    const float step = (endRad - startRad) / static_cast<float>(segments);
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = startRad + step * static_cast<float>(i);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        verts.push_back({ { pxCX + cosA * outerR, pxCY + sinA * outerR }, fc, { 0,0 } });
        verts.push_back({ { pxCX + cosA * innerR, pxCY + sinA * innerR }, fc, { 0,0 } });

        if (i > 0)
        {
            int bo = (i - 1) * 2;
            indices.push_back(bo);     indices.push_back(bo + 1); indices.push_back(bo + 2);
            indices.push_back(bo + 1); indices.push_back(bo + 3); indices.push_back(bo + 2);
        }
    }

    SDL_RenderGeometry(m_renderer, nullptr,
        verts.data(), static_cast<int>(verts.size()),
        indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawRoundedRect(NormRect rect, float normCornerRadius,
                                Color color, bool filled, int cornerSegments,
                                float normThickness)
{
    if (!m_renderer) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    const float pxX = rect.x      * sw;
    const float pxY = rect.y      * sh;
    const float pxW = rect.width  * sw;
    const float pxH = rect.height * sh;
    const float pxR = normCornerRadius * std::min(sw, sh);

    const float r  = std::min(pxR, std::min(pxW * 0.5f, pxH * 0.5f));
    const SDL_FColor fc = color.ToSDLFColor();

    if (filled)
    {
        // 三个填充矩形（水平中间 + 上/下带圆角的矩形通过三角扇补全）
        SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

        // 中心矩形（水平延展，高度 = pxH - 2r）
        SDL_FRect mid = { pxX, pxY + r, pxW, pxH - 2.0f * r };
        SDL_RenderFillRect(m_renderer, &mid);
        // 上/下横条（宽 = pxW - 2r，高 = r）
        SDL_FRect top = { pxX + r, pxY,              pxW - 2.0f * r, r };
        SDL_FRect bot = { pxX + r, pxY + pxH - r,   pxW - 2.0f * r, r };
        SDL_RenderFillRect(m_renderer, &top);
        SDL_RenderFillRect(m_renderer, &bot);

        // 四个角扇形
        struct CornerDef { float cx; float cy; float startDeg; };
        const CornerDef corners[4] = {
            { pxX + pxW - r, pxY + r,        -90.0f },   // 右上
            { pxX + r,       pxY + r,        -180.0f },  // 左上
            { pxX + r,       pxY + pxH - r,   180.0f },  // 左下
            { pxX + pxW - r, pxY + pxH - r,    0.0f }    // 右下
        };

        for (const auto& c : corners)
        {
            const float startRad = c.startDeg * kPi / 180.0f;
            const float endRad   = startRad + kPi * 0.5f;

            std::vector<SDL_Vertex> verts;
            std::vector<int> indices;
            BuildCircleGeometry(c.cx, c.cy, r, cornerSegments,
                                startRad, endRad, fc, verts, indices);
            SDL_RenderGeometry(m_renderer, nullptr,
                verts.data(), static_cast<int>(verts.size()),
                indices.data(), static_cast<int>(indices.size()));
        }
    }
    else
    {
        // 轮廓：四条直线 + 四段圆弧
        const float normT = normThickness;

        // 转换回归一化坐标绘制直线
        auto pxToNX = [sw](float px) { return px / sw; };
        auto pxToNY = [sh](float py) { return py / sh; };

        DrawLine(pxToNX(pxX + r),      pxToNY(pxY),          pxToNX(pxX + pxW - r), pxToNY(pxY),          color, normT);
        DrawLine(pxToNX(pxX + r),      pxToNY(pxY + pxH),    pxToNX(pxX + pxW - r), pxToNY(pxY + pxH),    color, normT);
        DrawLine(pxToNX(pxX),          pxToNY(pxY + r),       pxToNX(pxX),           pxToNY(pxY + pxH - r), color, normT);
        DrawLine(pxToNX(pxX + pxW),    pxToNY(pxY + r),       pxToNX(pxX + pxW),     pxToNY(pxY + pxH - r), color, normT);

        const float normR = r / std::min(static_cast<float>(sw), static_cast<float>(sh));
        DrawArc(pxToNX(pxX + pxW - r), pxToNY(pxY + r),        normR, -90.0f,   0.0f, color, normT, cornerSegments);
        DrawArc(pxToNX(pxX + r),       pxToNY(pxY + r),         normR, 180.0f, 270.0f, color, normT, cornerSegments);
        DrawArc(pxToNX(pxX + r),       pxToNY(pxY + pxH - r),  normR,  90.0f,  180.0f, color, normT, cornerSegments);
        DrawArc(pxToNX(pxX + pxW - r), pxToNY(pxY + pxH - r),  normR,   0.0f,   90.0f, color, normT, cornerSegments);
    }
}

} // namespace sakura::core
