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
