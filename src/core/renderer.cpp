#include "renderer.h"
#include "utils/logger.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <limits>
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

    SDL_GetCurrentRenderOutputSize(m_renderer, &m_width, &m_height);
    m_vertices.reserve(16384);
    m_indices.reserve(32768);
    // 启用 Alpha 混合
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    LOG_INFO("渲染器初始化成功，后端: {}", SDL_GetRendererName(m_renderer));
    return true;
}

void Renderer::Destroy()
{
    ReleaseTextResources();

    if (m_renderer)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
        LOG_DEBUG("渲染器已销毁");
    }
    m_window = nullptr;
}

void Renderer::ReleaseTextResources()
{
    Flush();
    ClearTextCache();
    for (auto& [key, font] : m_sizedFonts) TTF_CloseFont(font);
    m_sizedFonts.clear();
}

void Renderer::DrawPetal(float cx, float cy, float size, float rotation, Color color)
{
    constexpr int segments = 24;
    SDL_Vertex vertices[segments + 2]{};
    int indices[segments * 3]{};
    const float scale = size * std::min(m_width, m_height);
    const float c = std::cos(rotation), sn = std::sin(rotation);
    vertices[0] = {{cx * m_width, cy * m_height}, color.ToSDLFColor(), {0,0}};
    for (int i = 0; i <= segments; ++i)
    {
        const float a = static_cast<float>(i) / segments * 6.2831853f;
        float x = std::cos(a), y = std::sin(a) * 0.52f;
        if (i == 0 || i == segments) x = 0.72f; // characteristic split tip
        vertices[i + 1] = {{cx * m_width + (x * c - y * sn) * scale,
            cy * m_height + (x * sn + y * c) * scale}, color.ToSDLFColor(), {0,0}};
        if (i < segments) { indices[i*3] = 0; indices[i*3+1] = i+1; indices[i*3+2] = i+2; }
    }
    QueueGeometry(vertices, segments + 2, indices, segments * 3);
}

void Renderer::BeginFrame()
{
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(m_renderer, &width, &height);
    if (width != m_width || height != m_height)
    {
        Flush();
        ClearTextCache();
        for (auto& [key, font] : m_sizedFonts) TTF_CloseFont(font);
        m_sizedFonts.clear();
        m_width = width; m_height = height;
    }
}

void Renderer::EndFrame()
{
    Flush();
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
    Flush();
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
    if (color.a == 0 || rect.width <= 0 || rect.height <= 0) return;
    DrawGradientRect(rect, color, color, color, color);
}

void Renderer::DrawRectOutline(NormRect rect, Color color, float normThickness)
{
    const float px = normThickness * std::min(m_width, m_height);
    const float tx = px / std::max(1, m_width), ty = px / std::max(1, m_height);
    DrawFilledRect({rect.x, rect.y, rect.width, ty}, color);
    DrawFilledRect({rect.x, rect.y + rect.height - ty, rect.width, ty}, color);
    DrawFilledRect({rect.x, rect.y + ty, tx, rect.height - 2 * ty}, color);
    DrawFilledRect({rect.x + rect.width - tx, rect.y + ty, tx, rect.height - 2 * ty}, color);
}

void Renderer::DrawGradientRect(NormRect rect, Color colorTopLeft, Color colorTopRight, Color colorBottomLeft, Color colorBottomRight)
{
    if (!m_renderer) return;

    SDL_FRect dstRect = rect.ToPixel(GetScreenWidth(), GetScreenHeight());

    SDL_Vertex verts[4];
    verts[0].position = { dstRect.x, dstRect.y };
    verts[0].color = colorTopLeft.ToSDLFColor();
    verts[0].tex_coord = {0.0f, 0.0f};

    verts[1].position = { dstRect.x + dstRect.w, dstRect.y };
    verts[1].color = colorTopRight.ToSDLFColor();
    verts[1].tex_coord = {1.0f, 0.0f};

    verts[2].position = { dstRect.x + dstRect.w, dstRect.y + dstRect.h };
    verts[2].color = colorBottomRight.ToSDLFColor();
    verts[2].tex_coord = {1.0f, 1.0f};

    verts[3].position = { dstRect.x, dstRect.y + dstRect.h };
    verts[3].color = colorBottomLeft.ToSDLFColor();
    verts[3].tex_coord = {0.0f, 1.0f};

    int indices[6] = {0, 1, 2, 0, 2, 3};

    QueueGeometry(verts, 4, indices, 6);
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
    Flush();
    SDL_SetRenderDrawBlendMode(m_renderer, sdlMode);
}

// ── 屏幕信息 ──────────────────────────────────────────────────────────────────

int Renderer::GetScreenWidth() const { return m_width; }
int Renderer::GetScreenHeight() const { return m_height; }

void Renderer::Flush() const
{
    if (m_renderer && !m_vertices.empty())
        SDL_RenderGeometry(m_renderer, nullptr, m_vertices.data(), static_cast<int>(m_vertices.size()),
            m_indices.data(), static_cast<int>(m_indices.size()));
    m_vertices.clear();
    m_indices.clear();
}

void Renderer::QueueGeometry(const SDL_Vertex* vertices, int vertexCount, const int* indices, int indexCount)
{
    if (!m_renderer) return;
    if (m_vertices.size() + vertexCount > 16000) Flush();
    const int base = static_cast<int>(m_vertices.size());
    m_vertices.insert(m_vertices.end(), vertices, vertices + vertexCount);
    for (int i = 0; i < indexCount; ++i) m_indices.push_back(base + indices[i]);
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

    const int screenH = GetScreenHeight();
    const int screenW = GetScreenWidth();
    const int pixelFontSize = std::max(1, static_cast<int>(std::lround(
        normFontSize * static_cast<float>(screenH))));

    TextCacheEntry* entry = GetOrCreateTextCacheEntry(fontHandle, text, pixelFontSize);
    if (!entry || !entry->texture)
        return;

    // 根据对齐方式计算左上角像素坐标
    float pxX = normX * static_cast<float>(screenW);
    float pxY = normY * static_cast<float>(screenH);

    switch (align)
    {
        case TextAlign::Center:
            pxX -= entry->width * 0.5f;
            break;
        case TextAlign::Right:
            pxX -= entry->width;
            break;
        case TextAlign::Left:
        default:
            break;
    }

    SDL_FRect dest = { pxX, pxY, entry->width, entry->height };
    SDL_SetTextureColorMod(entry->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(entry->texture, color.a);
    Flush();
    SDL_RenderTexture(m_renderer, entry->texture, nullptr, &dest);
    SDL_SetTextureColorMod(entry->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(entry->texture, 255);
}

float Renderer::MeasureTextWidth(FontHandle fontHandle,
                                  std::string_view text,
                                  float normFontSize) const
{
    return MeasureText(fontHandle, text, normFontSize).width;
}

TextMetrics Renderer::MeasureText(FontHandle fontHandle,
                                  std::string_view text,
                                  float normFontSize) const
{
    if (!m_renderer || text.empty()) return {};

    const int pixelSize = std::max(1, static_cast<int>(std::lround(normFontSize * m_height)));
    auto* entry = const_cast<Renderer*>(this)->GetOrCreateTextCacheEntry(fontHandle, text, pixelSize);
    if (!entry || m_width <= 0 || m_height <= 0) return {};
    return {entry->width / m_width, entry->height / m_height};
}

Renderer::TextCacheEntry* Renderer::GetOrCreateTextCacheEntry(FontHandle fontHandle,
                                                              std::string_view text,
                                                              int pixelFontSize)
{
    if (!m_renderer || text.empty())
        return nullptr;

    std::string key = std::to_string(fontHandle);
    key.push_back('|');
    key += std::to_string(pixelFontSize);
    key.push_back('|');
    key.append(text.data(), text.size());

    auto it = m_textCache.find(key);
    if (it != m_textCache.end())
    {
        it->second.lastUsed = ++m_textCacheUseCounter;
        return &it->second;
    }

    const uint64_t fontKey = (static_cast<uint64_t>(fontHandle) << 32) | static_cast<uint32_t>(pixelFontSize);
    TTF_Font* font = nullptr;
    if (const auto cached = m_sizedFonts.find(fontKey); cached != m_sizedFonts.end()) font = cached->second;
    else
    {
        auto* base = ResourceManager::GetInstance().GetFont(fontHandle);
        if (!base) return nullptr;
        font = TTF_CopyFont(base);
        if (!font) return nullptr;
        TTF_SetFontSize(font, static_cast<float>(pixelFontSize));
        // Bound font copies as well as textures; animated headings can use many sizes.
        if (m_sizedFonts.size() >= 96)
        {
            TTF_CloseFont(m_sizedFonts.begin()->second);
            m_sizedFonts.erase(m_sizedFonts.begin());
        }
        m_sizedFonts.emplace(fontKey, font);
    }
    SDL_Color white = {255, 255, 255, 255};
    const std::string textStr(text);
    SDL_Surface* surface = TTF_RenderText_Blended(font, textStr.c_str(), 0, white);

    if (!surface)
    {
        LOG_WARN("TTF_RenderText_Blended 失败: {}", SDL_GetError());
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture)
    {
        LOG_WARN("SDL_CreateTextureFromSurface 失败: {}", SDL_GetError());
        return nullptr;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    float texW = 0.0f;
    float texH = 0.0f;
    SDL_GetTextureSize(texture, &texW, &texH);

    TrimTextCache();

    auto [insertedIt, inserted] = m_textCache.emplace(std::move(key), TextCacheEntry{
        texture,
        texW,
        texH,
        ++m_textCacheUseCounter
    });
    if (!inserted)
    {
        SDL_DestroyTexture(texture);
        insertedIt->second.lastUsed = ++m_textCacheUseCounter;
    }

    return &insertedIt->second;
}

void Renderer::TrimTextCache()
{
    while (m_textCache.size() >= MAX_TEXT_CACHE_ENTRIES)
    {
        auto oldestIt = m_textCache.end();
        uint64_t oldestUse = std::numeric_limits<uint64_t>::max();

        for (auto it = m_textCache.begin(); it != m_textCache.end(); ++it)
        {
            if (it->second.lastUsed < oldestUse)
            {
                oldestUse = it->second.lastUsed;
                oldestIt = it;
            }
        }

        if (oldestIt == m_textCache.end())
            break;

        if (oldestIt->second.texture)
            SDL_DestroyTexture(oldestIt->second.texture);
        m_textCache.erase(oldestIt);
    }
}

void Renderer::ClearTextCache()
{
    for (auto& [key, entry] : m_textCache)
    {
        if (entry.texture)
            SDL_DestroyTexture(entry.texture);
    }
    m_textCache.clear();
    m_textCacheUseCounter = 0;
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

    Flush();
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
    Flush();
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

    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear(); indices.clear();
    verts.reserve(static_cast<size_t>(segments + 2));
    indices.reserve(static_cast<size_t>(segments * 3));

    BuildCircleGeometry(pxCX, pxCY, pxR, segments, 0.0f, kPi * 2.0f, fc, verts, indices);

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
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
    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear(); indices.clear();
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

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
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

    QueueGeometry(verts, 4, indices, 6);
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

    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear(); indices.clear();
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

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
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
        // 中心矩形（水平延展，高度 = pxH - 2r）
        SDL_FRect mid = { pxX, pxY + r, pxW, pxH - 2.0f * r };
        DrawFilledRect({mid.x / sw, mid.y / sh, mid.w / sw, mid.h / sh}, color);
        // 上/下横条（宽 = pxW - 2r，高 = r）
        SDL_FRect top = { pxX + r, pxY,              pxW - 2.0f * r, r };
        SDL_FRect bot = { pxX + r, pxY + pxH - r,   pxW - 2.0f * r, r };
        DrawFilledRect({top.x / sw, top.y / sh, top.w / sw, top.h / sh}, color);
        DrawFilledRect({bot.x / sw, bot.y / sh, bot.w / sw, bot.h / sh}, color);

        // 四个角扇形
        struct CornerDef { float cx; float cy; float startDeg; };
        const CornerDef corners[4] = {
            { pxX + pxW - r, pxY + r,        -90.0f },   // 右上
            { pxX + r,       pxY + r,        -180.0f },  // 左上
            { pxX + r,       pxY + pxH - r,    90.0f },  // 左下
            { pxX + pxW - r, pxY + pxH - r,    0.0f }    // 右下
        };

        for (const auto& c : corners)
        {
            const float startRad = c.startDeg * kPi / 180.0f;
            const float endRad   = startRad + kPi * 0.5f;

            static thread_local std::vector<SDL_Vertex> verts;
            static thread_local std::vector<int> indices;
            verts.clear(); indices.clear();
            BuildCircleGeometry(c.cx, c.cy, r, cornerSegments,
                                startRad, endRad, fc, verts, indices);
            QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
        }
    }
    else
    {
        DrawRoundedRectOutlineGradient(rect, normCornerRadius, color, color, normThickness, cornerSegments);
    }
}

namespace
{

SDL_FColor LerpFColor(const SDL_FColor& a, const SDL_FColor& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

SDL_FColor BilerpFColor(const SDL_FColor& tl, const SDL_FColor& tr,
                        const SDL_FColor& bl, const SDL_FColor& br,
                        float u, float v)
{
    const SDL_FColor top = LerpFColor(tl, tr, u);
    const SDL_FColor bot = LerpFColor(bl, br, u);
    return LerpFColor(top, bot, v);
}

Color LerpColor8(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<uint8_t>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<uint8_t>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<uint8_t>(std::lround(a.b + (b.b - a.b) * t)),
        static_cast<uint8_t>(std::lround(a.a + (b.a - a.a) * t))
    };
}

struct RoundedPerimeterPoint
{
    float x  = 0.0f;
    float y  = 0.0f;
    float nx = 0.0f;
    float ny = 0.0f;
};

void BuildRoundedPerimeter(float pxX, float pxY, float pxW, float pxH, float r,
                           int cornerSegments, std::vector<RoundedPerimeterPoint>& outPts)
{
    outPts.clear();
    const int segs = std::max(2, cornerSegments);
    outPts.reserve(static_cast<size_t>((segs + 1) * 4));

    struct CornerArc { float cx; float cy; float startRad; };
    const CornerArc arcs[4] = {
        { pxX + pxW - r, pxY + r,       -kPi * 0.5f }, // 右上: -90 -> 0
        { pxX + pxW - r, pxY + pxH - r,  0.0f       }, // 右下: 0 -> 90
        { pxX + r,       pxY + pxH - r,  kPi * 0.5f }, // 左下: 90 -> 180
        { pxX + r,       pxY + r,        kPi        }  // 左上: 180 -> 270
    };

    for (int c = 0; c < 4; ++c)
    {
        for (int i = 0; i <= segs; ++i)
        {
            const float a = arcs[c].startRad + (kPi * 0.5f) * (static_cast<float>(i) / static_cast<float>(segs));
            const float nx = std::cos(a);
            const float ny = std::sin(a);
            outPts.push_back({ arcs[c].cx + nx * r, arcs[c].cy + ny * r, nx, ny });
        }
    }
}

} // namespace

void Renderer::DrawRoundedGradientRect(NormRect rect, float normCornerRadius,
                                       Color topLeft, Color topRight,
                                       Color bottomLeft, Color bottomRight,
                                       int cornerSegments, bool antiAliased)
{
    if (!m_renderer || rect.width <= 0.0f || rect.height <= 0.0f) return;
    if (topLeft.a == 0 && topRight.a == 0 && bottomLeft.a == 0 && bottomRight.a == 0) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (sw <= 0 || sh <= 0) return;

    const float pxX = rect.x * static_cast<float>(sw);
    const float pxY = rect.y * static_cast<float>(sh);
    const float pxW = rect.width * static_cast<float>(sw);
    const float pxH = rect.height * static_cast<float>(sh);
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float r = std::clamp(normCornerRadius * minDim, 0.0f, std::min(pxW, pxH) * 0.5f);

    const SDL_FColor tl = topLeft.ToSDLFColor();
    const SDL_FColor tr = topRight.ToSDLFColor();
    const SDL_FColor bl = bottomLeft.ToSDLFColor();
    const SDL_FColor br = bottomRight.ToSDLFColor();

    static thread_local std::vector<RoundedPerimeterPoint> perim;
    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    BuildRoundedPerimeter(pxX, pxY, pxW, pxH, r, cornerSegments, perim);
    const int n = static_cast<int>(perim.size());
    if (n < 4) return;

    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>(1 + n * (antiAliased ? 2 : 1)));
    indices.reserve(static_cast<size_t>(n * (antiAliased ? 9 : 3)));

    auto sampleAt = [&](float x, float y) -> SDL_FColor {
        const float u = (pxW > 0.001f) ? std::clamp((x - pxX) / pxW, 0.0f, 1.0f) : 0.5f;
        const float v = (pxH > 0.001f) ? std::clamp((y - pxY) / pxH, 0.0f, 1.0f) : 0.5f;
        return BilerpFColor(tl, tr, bl, br, u, v);
    };

    const float cx = pxX + pxW * 0.5f;
    const float cy = pxY + pxH * 0.5f;
    const float feather = antiAliased ? 0.65f : 0.0f;
    verts.push_back({ { cx, cy }, sampleAt(cx, cy), { 0.5f, 0.5f } });

    for (int i = 0; i < n; ++i)
    {
        const float ix = perim[i].x - perim[i].nx * (feather * 0.5f);
        const float iy = perim[i].y - perim[i].ny * (feather * 0.5f);
        verts.push_back({ { ix, iy }, sampleAt(perim[i].x, perim[i].y), { 0.0f, 0.0f } });
    }

    for (int i = 0; i < n; ++i)
    {
        const int next = (i + 1) % n;
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + next);
    }

    if (antiAliased)
    {
        const int outerBase = 1 + n;
        for (int i = 0; i < n; ++i)
        {
            SDL_FColor edgeCol = sampleAt(perim[i].x, perim[i].y);
            edgeCol.a = 0.0f;
            const float ox = perim[i].x + perim[i].nx * (feather * 0.85f);
            const float oy = perim[i].y + perim[i].ny * (feather * 0.85f);
            verts.push_back({ { ox, oy }, edgeCol, { 0.0f, 0.0f } });
        }
        for (int i = 0; i < n; ++i)
        {
            const int next = (i + 1) % n;
            const int i0 = 1 + i;
            const int i1 = 1 + next;
            const int o0 = outerBase + i;
            const int o1 = outerBase + next;
            indices.push_back(i0); indices.push_back(o0); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(o0); indices.push_back(o1);
        }
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawRoundedRectOutlineGradient(NormRect rect, float normCornerRadius,
                                              Color topColor, Color bottomColor,
                                              float normThickness, int cornerSegments)
{
    if (!m_renderer || rect.width <= 0.0f || rect.height <= 0.0f) return;
    if (topColor.a == 0 && bottomColor.a == 0) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (sw <= 0 || sh <= 0) return;

    const float pxX = rect.x * static_cast<float>(sw);
    const float pxY = rect.y * static_cast<float>(sh);
    const float pxW = rect.width * static_cast<float>(sw);
    const float pxH = rect.height * static_cast<float>(sh);
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float r = std::clamp(normCornerRadius * minDim, 0.0f, std::min(pxW, pxH) * 0.5f);
    const float halfT = std::max(0.5f, normThickness * minDim * 0.5f);
    const float feather = 0.65f;

    const SDL_FColor tc = topColor.ToSDLFColor();
    const SDL_FColor bc = bottomColor.ToSDLFColor();

    static thread_local std::vector<RoundedPerimeterPoint> perim;
    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    BuildRoundedPerimeter(pxX, pxY, pxW, pxH, r, cornerSegments, perim);
    const int n = static_cast<int>(perim.size());
    if (n < 4) return;

    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>(n * 4));
    indices.reserve(static_cast<size_t>(n * 18));

    for (int i = 0; i < n; ++i)
    {
        const float v = (pxH > 0.001f) ? std::clamp((perim[i].y - pxY) / pxH, 0.0f, 1.0f) : 0.5f;
        const SDL_FColor midCol = LerpFColor(tc, bc, v);
        SDL_FColor zeroCol = midCol;
        zeroCol.a = 0.0f;

        const float d0 = -halfT - feather;
        const float d1 = -halfT;
        const float d2 =  halfT;
        const float d3 =  halfT + feather;

        verts.push_back({ { perim[i].x + perim[i].nx * d0, perim[i].y + perim[i].ny * d0 }, zeroCol, { 0.0f, 0.0f } });
        verts.push_back({ { perim[i].x + perim[i].nx * d1, perim[i].y + perim[i].ny * d1 }, midCol,  { 0.0f, 0.0f } });
        verts.push_back({ { perim[i].x + perim[i].nx * d2, perim[i].y + perim[i].ny * d2 }, midCol,  { 0.0f, 0.0f } });
        verts.push_back({ { perim[i].x + perim[i].nx * d3, perim[i].y + perim[i].ny * d3 }, zeroCol, { 0.0f, 0.0f } });
    }

    for (int i = 0; i < n; ++i)
    {
        const int next = (i + 1) % n;
        for (int band = 0; band < 3; ++band)
        {
            const int a0 = i * 4 + band;
            const int a1 = i * 4 + band + 1;
            const int b0 = next * 4 + band;
            const int b1 = next * 4 + band + 1;
            indices.push_back(a0); indices.push_back(a1); indices.push_back(b0);
            indices.push_back(b0); indices.push_back(a1); indices.push_back(b1);
        }
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawSoftShadow(NormRect rect, float normCornerRadius,
                              float normSpread, Color shadowColor,
                              float normOffsetY, int cornerSegments)
{
    if (!m_renderer || shadowColor.a == 0 || normSpread <= 0.0f) return;
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (sw <= 0 || sh <= 0) return;

    const float minDim = static_cast<float>(std::min(sw, sh));
    const float pxX = rect.x * static_cast<float>(sw);
    const float pxY = (rect.y + normOffsetY) * static_cast<float>(sh);
    const float pxW = rect.width * static_cast<float>(sw);
    const float pxH = rect.height * static_cast<float>(sh);
    const float r = std::clamp(normCornerRadius * minDim, 0.0f, std::min(pxW, pxH) * 0.5f);
    const float spreadPx = normSpread * minDim;

    const SDL_FColor innerCol = shadowColor.ToSDLFColor();
    SDL_FColor midCol = innerCol;
    midCol.a *= 0.36f;
    SDL_FColor outerCol = innerCol;
    outerCol.a = 0.0f;

    static thread_local std::vector<RoundedPerimeterPoint> perim;
    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    BuildRoundedPerimeter(pxX, pxY, pxW, pxH, r, cornerSegments, perim);
    const int n = static_cast<int>(perim.size());
    if (n < 4) return;

    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>(1 + n * 3));
    indices.reserve(static_cast<size_t>(n * 15));

    const float cx = pxX + pxW * 0.5f;
    const float cy = pxY + pxH * 0.5f;
    verts.push_back({ { cx, cy }, innerCol, { 0.5f, 0.5f } });

    for (int i = 0; i < n; ++i)
    {
        verts.push_back({ { perim[i].x, perim[i].y }, innerCol, { 0.0f, 0.0f } });
        verts.push_back({ { perim[i].x + perim[i].nx * (spreadPx * 0.42f), perim[i].y + perim[i].ny * (spreadPx * 0.42f) }, midCol, { 0.0f, 0.0f } });
        verts.push_back({ { perim[i].x + perim[i].nx * spreadPx, perim[i].y + perim[i].ny * spreadPx }, outerCol, { 0.0f, 0.0f } });
    }

    for (int i = 0; i < n; ++i)
    {
        const int next = (i + 1) % n;
        const int b0 = 1 + i * 3;
        const int b1 = 1 + next * 3;
        indices.push_back(0); indices.push_back(b0); indices.push_back(b1);
        for (int band = 0; band < 2; ++band)
        {
            indices.push_back(b0 + band);     indices.push_back(b0 + band + 1); indices.push_back(b1 + band);
            indices.push_back(b1 + band);     indices.push_back(b0 + band + 1); indices.push_back(b1 + band + 1);
        }
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawRadialGradient(float cx, float cy,
                                  float normRadiusX, float normRadiusY,
                                  Color innerColor, Color outerColor,
                                  int segments)
{
    if (!m_renderer || (innerColor.a == 0 && outerColor.a == 0)) return;
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float pxCX = cx * static_cast<float>(sw);
    const float pxCY = cy * static_cast<float>(sh);
    const float rx = normRadiusX * minDim;
    const float ry = normRadiusY * minDim;
    if (rx <= 0.1f || ry <= 0.1f) return;

    const int n = std::max(8, segments);
    const SDL_FColor ic = innerColor.ToSDLFColor();
    const SDL_FColor oc = outerColor.ToSDLFColor();
    const SDL_FColor mc = LerpFColor(ic, oc, 0.58f);

    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>(1 + (n + 1) * 2));
    indices.reserve(static_cast<size_t>(n * 9));

    verts.push_back({ { pxCX, pxCY }, ic, { 0.5f, 0.5f } });
    for (int i = 0; i <= n; ++i)
    {
        const float a = (kPi * 2.0f * static_cast<float>(i)) / static_cast<float>(n);
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        verts.push_back({ { pxCX + ca * rx * 0.52f, pxCY + sa * ry * 0.52f }, mc, { 0.5f, 0.5f } });
        verts.push_back({ { pxCX + ca * rx,         pxCY + sa * ry         }, oc, { 0.5f, 0.5f } });
        if (i > 0)
        {
            const int prevMid = 1 + (i - 1) * 2;
            const int currMid = 1 + i * 2;
            indices.push_back(0);       indices.push_back(prevMid);     indices.push_back(currMid);
            indices.push_back(prevMid); indices.push_back(prevMid + 1); indices.push_back(currMid);
            indices.push_back(currMid); indices.push_back(prevMid + 1); indices.push_back(currMid + 1);
        }
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawRingGradient(float cx, float cy,
                                float normInnerRadius, float normOuterRadius,
                                Color innerColor, Color outerColor,
                                int segments,
                                float startAngleDeg, float endAngleDeg)
{
    if (!m_renderer || normOuterRadius <= normInnerRadius) return;
    if (innerColor.a == 0 && outerColor.a == 0) return;

    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float pxCX = cx * static_cast<float>(sw);
    const float pxCY = cy * static_cast<float>(sh);
    const float inR = std::max(0.0f, normInnerRadius * minDim);
    const float outR = normOuterRadius * minDim;

    const int n = std::max(6, segments);
    const float sRad = startAngleDeg * kPi / 180.0f;
    const float eRad = endAngleDeg * kPi / 180.0f;
    const SDL_FColor ic = innerColor.ToSDLFColor();
    const SDL_FColor oc = outerColor.ToSDLFColor();

    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>((n + 1) * 2));
    indices.reserve(static_cast<size_t>(n * 6));

    for (int i = 0; i <= n; ++i)
    {
        const float a = sRad + (eRad - sRad) * (static_cast<float>(i) / static_cast<float>(n));
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        verts.push_back({ { pxCX + ca * inR,  pxCY + sa * inR  }, ic, { 0.0f, 0.0f } });
        verts.push_back({ { pxCX + ca * outR, pxCY + sa * outR }, oc, { 0.0f, 0.0f } });
        if (i > 0)
        {
            const int b = (i - 1) * 2;
            indices.push_back(b);     indices.push_back(b + 1); indices.push_back(b + 2);
            indices.push_back(b + 2); indices.push_back(b + 1); indices.push_back(b + 3);
        }
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawQuad(NormPoint p0, NormPoint p1, NormPoint p2, NormPoint p3,
                        Color c0, Color c1, Color c2, Color c3)
{
    if (!m_renderer) return;
    const float sw = static_cast<float>(GetScreenWidth());
    const float sh = static_cast<float>(GetScreenHeight());
    SDL_Vertex verts[4] = {
        { { p0.x * sw, p0.y * sh }, c0.ToSDLFColor(), { 0.0f, 0.0f } },
        { { p1.x * sw, p1.y * sh }, c1.ToSDLFColor(), { 1.0f, 0.0f } },
        { { p2.x * sw, p2.y * sh }, c2.ToSDLFColor(), { 1.0f, 1.0f } },
        { { p3.x * sw, p3.y * sh }, c3.ToSDLFColor(), { 0.0f, 1.0f } }
    };
    const int indices[6] = { 0, 1, 2, 0, 2, 3 };
    QueueGeometry(verts, 4, indices, 6);
}

void Renderer::DrawSpriteQuad(TextureHandle texHandle,
                              NormPoint p0, NormPoint p1, NormPoint p2, NormPoint p3,
                              Color tint, float alpha)
{
    SDL_Texture* tex = ResourceManager::GetInstance().GetTexture(texHandle);
    if (!tex || !m_renderer || alpha <= 0.001f) return;

    Flush();
    const float sw = static_cast<float>(GetScreenWidth());
    const float sh = static_cast<float>(GetScreenHeight());
    SDL_FColor fc = tint.ToSDLFColor();
    fc.a *= std::clamp(alpha, 0.0f, 1.0f);

    SDL_Vertex verts[4] = {
        { { p0.x * sw, p0.y * sh }, fc, { 0.0f, 0.0f } },
        { { p1.x * sw, p1.y * sh }, fc, { 1.0f, 0.0f } },
        { { p2.x * sw, p2.y * sh }, fc, { 1.0f, 1.0f } },
        { { p3.x * sw, p3.y * sh }, fc, { 0.0f, 1.0f } }
    };
    const int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(m_renderer, tex, verts, 4, indices, 6);
}

void Renderer::DrawRibbonStrip(const RibbonNode* nodes, int count, bool roundedCaps)
{
    if (!m_renderer || !nodes || count < 2) return;
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (sw <= 0 || sh <= 0) return;
    const float fsw = static_cast<float>(sw);
    const float fsh = static_cast<float>(sh);
    const float minDim = static_cast<float>(std::min(sw, sh));

    struct PxNode
    {
        float x = 0.0f;
        float y = 0.0f;
        float halfW = 0.0f;
        SDL_FColor core{};
        SDL_FColor edge{};
    };

    static thread_local std::vector<PxNode> pts;
    pts.clear();
    pts.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const float px = nodes[i].x * fsw;
        const float py = nodes[i].y * fsh;
        if (!pts.empty())
        {
            const float dx = px - pts.back().x;
            const float dy = py - pts.back().y;
            if (dx * dx + dy * dy < 0.09f && i < count - 1) continue;
        }
        pts.push_back({
            px, py,
            std::max(0.5f, nodes[i].normWidth * minDim * 0.5f),
            nodes[i].coreColor.ToSDLFColor(),
            nodes[i].edgeColor.ToSDLFColor()
        });
    }

    const int n = static_cast<int>(pts.size());
    if (n < 2) return;

    static thread_local std::vector<SDL_Vertex> verts;
    static thread_local std::vector<int> indices;
    verts.clear();
    indices.clear();
    verts.reserve(static_cast<size_t>(n * 4 + 36));
    indices.reserve(static_cast<size_t>((n - 1) * 18 + 96));

    auto segNormal = [&](int idxA, int idxB, float& outNx, float& outNy) {
        const float dx = pts[idxB].x - pts[idxA].x;
        const float dy = pts[idxB].y - pts[idxA].y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.0001f) { outNx = -dy / len; outNy = dx / len; }
        else { outNx = 0.0f; outNy = 1.0f; }
    };

    for (int i = 0; i < n; ++i)
    {
        float nx = 0.0f, ny = 1.0f;
        if (i == 0)
        {
            segNormal(0, 1, nx, ny);
        }
        else if (i == n - 1)
        {
            segNormal(n - 2, n - 1, nx, ny);
        }
        else
        {
            float n0x = 0.0f, n0y = 1.0f, n1x = 0.0f, n1y = 1.0f;
            segNormal(i - 1, i, n0x, n0y);
            segNormal(i, i + 1, n1x, n1y);
            float mx = n0x + n1x;
            float my = n0y + n1y;
            const float mlen = std::sqrt(mx * mx + my * my);
            if (mlen > 0.001f)
            {
                mx /= mlen;
                my /= mlen;
                const float dot = std::max(0.45f, mx * n1x + my * n1y);
                const float miter = std::min(1.85f, 1.0f / dot);
                nx = mx * miter;
                ny = my * miter;
            }
            else
            {
                nx = n1x;
                ny = n1y;
            }
        }

        const float hw = pts[i].halfW;
        const float cw = hw * 0.34f;
        verts.push_back({ { pts[i].x - nx * hw, pts[i].y - ny * hw }, pts[i].edge, { 0.0f, 0.0f } });
        verts.push_back({ { pts[i].x - nx * cw, pts[i].y - ny * cw }, pts[i].core, { 0.35f, 0.0f } });
        verts.push_back({ { pts[i].x + nx * cw, pts[i].y + ny * cw }, pts[i].core, { 0.65f, 0.0f } });
        verts.push_back({ { pts[i].x + nx * hw, pts[i].y + ny * hw }, pts[i].edge, { 1.0f, 0.0f } });
    }

    for (int i = 0; i < n - 1; ++i)
    {
        for (int band = 0; band < 3; ++band)
        {
            const int a0 = i * 4 + band;
            const int a1 = i * 4 + band + 1;
            const int b0 = (i + 1) * 4 + band;
            const int b1 = (i + 1) * 4 + band + 1;
            indices.push_back(a0); indices.push_back(a1); indices.push_back(b0);
            indices.push_back(b0); indices.push_back(a1); indices.push_back(b1);
        }
    }

    if (roundedCaps)
    {
        auto addCap = [&](const PxNode& capPt, float dirX, float dirY) {
            const float baseAng = std::atan2(dirY, dirX);
            constexpr int capSegs = 10;
            const int centerIdx = static_cast<int>(verts.size());
            verts.push_back({ { capPt.x, capPt.y }, capPt.core, { 0.5f, 0.5f } });
            for (int s = 0; s <= capSegs; ++s)
            {
                const float a = baseAng - kPi * 0.5f + kPi * (static_cast<float>(s) / static_cast<float>(capSegs));
                const float ca = std::cos(a);
                const float sa = std::sin(a);
                verts.push_back({ { capPt.x + ca * capPt.halfW * 0.36f, capPt.y + sa * capPt.halfW * 0.36f }, capPt.core, { 0.5f, 0.5f } });
                verts.push_back({ { capPt.x + ca * capPt.halfW,         capPt.y + sa * capPt.halfW         }, capPt.edge, { 0.5f, 0.5f } });
                if (s > 0)
                {
                    const int pm = centerIdx + 1 + (s - 1) * 2;
                    const int cm = centerIdx + 1 + s * 2;
                    indices.push_back(centerIdx); indices.push_back(pm);     indices.push_back(cm);
                    indices.push_back(pm);        indices.push_back(pm + 1); indices.push_back(cm);
                    indices.push_back(cm);        indices.push_back(pm + 1); indices.push_back(cm + 1);
                }
            }
        };

        float sdx = pts[0].x - pts[1].x, sdy = pts[0].y - pts[1].y;
        const float slen = std::max(0.0001f, std::sqrt(sdx * sdx + sdy * sdy));
        addCap(pts[0], sdx / slen, sdy / slen);

        float edx = pts[n - 1].x - pts[n - 2].x, edy = pts[n - 1].y - pts[n - 2].y;
        const float elen = std::max(0.0001f, std::sqrt(edx * edx + edy * edy));
        addCap(pts[n - 1], edx / elen, edy / elen);
    }

    QueueGeometry(verts.data(), static_cast<int>(verts.size()), indices.data(), static_cast<int>(indices.size()));
}

void Renderer::DrawCatmullRomRibbon(const NormPoint* controlPoints, int count,
                                    float startNormWidth, float endNormWidth,
                                    Color coreStart, Color coreEnd,
                                    Color auraStart, Color auraEnd,
                                    int subdivisionsPerSegment,
                                    bool roundedCaps)
{
    if (!m_renderer || !controlPoints || count < 2) return;

    static thread_local std::vector<RibbonNode> ribbon;
    ribbon.clear();

    if (count == 2)
    {
        ribbon.push_back({ controlPoints[0].x, controlPoints[0].y, startNormWidth, coreStart, auraStart });
        ribbon.push_back({ controlPoints[1].x, controlPoints[1].y, endNormWidth,   coreEnd,   auraEnd   });
        DrawRibbonStrip(ribbon.data(), 2, roundedCaps);
        return;
    }

    const int subs = std::clamp(subdivisionsPerSegment, 2, 24);
    const int totalSamples = (count - 1) * subs + 1;
    ribbon.reserve(static_cast<size_t>(totalSamples));

    auto catmull = [](float p0, float p1, float p2, float p3, float t) -> float {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * p1)
                     + (-p0 + p2) * t
                     + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                     + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    };

    for (int seg = 0; seg < count - 1; ++seg)
    {
        const NormPoint p1 = controlPoints[seg];
        const NormPoint p2 = controlPoints[seg + 1];
        const NormPoint p0 = (seg > 0) ? controlPoints[seg - 1]
                                       : NormPoint{ p1.x * 2.0f - p2.x, p1.y * 2.0f - p2.y };
        const NormPoint p3 = (seg + 2 < count) ? controlPoints[seg + 2]
                                               : NormPoint{ p2.x * 2.0f - p1.x, p2.y * 2.0f - p1.y };

        const int limit = (seg == count - 2) ? subs : (subs - 1);
        for (int s = 0; s <= limit; ++s)
        {
            const float localT = static_cast<float>(s) / static_cast<float>(subs);
            const float globalT = (static_cast<float>(seg) + localT) / static_cast<float>(count - 1);
            const float rx = catmull(p0.x, p1.x, p2.x, p3.x, localT);
            const float ry = catmull(p0.y, p1.y, p2.y, p3.y, localT);
            const float rw = startNormWidth + (endNormWidth - startNormWidth) * globalT;
            ribbon.push_back({
                rx, ry, rw,
                LerpColor8(coreStart, coreEnd, globalT),
                LerpColor8(auraStart, auraEnd, globalT)
            });
        }
    }

    DrawRibbonStrip(ribbon.data(), static_cast<int>(ribbon.size()), roundedCaps);
}

} // namespace sakura::core
