#pragma once

#include <SDL3/SDL.h>
#include "resource_manager.h"
#include <cstdint>
#include <string_view>

namespace sakura::core
{

// ============================================================================
// Color — RGBA 颜色
// ============================================================================
struct Color
{
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    constexpr Color() = default;
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    SDL_FColor ToSDLFColor() const
    {
        return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }

    Color WithAlpha(uint8_t newAlpha) const { return { r, g, b, newAlpha }; }

    // 预制颜色
    static const Color White;
    static const Color Black;
    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Yellow;
    static const Color Cyan;
    static const Color Magenta;
    static const Color Transparent;
    static const Color DarkBlue;    // 深蓝色背景 (15,15,35)
};

// ============================================================================
// NormRect — 归一化矩形 (x,y,width,height 均为 0.0~1.0)
// ============================================================================
struct NormRect
{
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 1.0f;
    float height = 1.0f;

    constexpr NormRect() = default;
    constexpr NormRect(float x, float y, float w, float h)
        : x(x), y(y), width(w), height(h) {}

    // 归一化坐标 → 像素矩形
    SDL_FRect ToPixel(int screenW, int screenH) const
    {
        return {
            x      * static_cast<float>(screenW),
            y      * static_cast<float>(screenH),
            width  * static_cast<float>(screenW),
            height * static_cast<float>(screenH)
        };
    }
};

// ============================================================================
// TextAlign — 文字对齐方式
// ============================================================================
enum class TextAlign
{
    Left,
    Center,
    Right
};

// ============================================================================
// BlendMode — 混合模式
// ============================================================================
enum class BlendMode
{
    None,
    Alpha,
    Additive,
    Multiply
};

// ============================================================================
// Renderer — SDL3 GPU 加速的 2D 渲染器
// ============================================================================
class Renderer
{
public:
    Renderer();
    ~Renderer();

    // 不允许拷贝
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // 初始化，绑定到指定窗口
    bool Initialize(SDL_Window* window);

    // 销毁渲染器
    void Destroy();

    // ── 帧管理 ────────────────────────────────────────────────────────────────

    // 开始新一帧
    void BeginFrame();

    // 结束帧，提交渲染
    void EndFrame();

    // 设置清屏颜色并清屏
    void Clear(Color color = Color::DarkBlue);

    // ── 归一化坐标转换 ────────────────────────────────────────────────────────

    float ToPixelX(float normX) const;
    float ToPixelY(float normY) const;
    float ToPixelW(float normW) const;
    float ToPixelH(float normH) const;

    // ── 基础图形绘制（归一化坐标）────────────────────────────────────────────

    void DrawFilledRect(NormRect rect, Color color);
    void DrawRectOutline(NormRect rect, Color color, float normThickness = 0.002f);

    // ── 文字渲染 ──────────────────────────────────────────────────────────────

    // normFontSize: 字号相对屏幕高度的比例（0.03 = 屏幕高度的 3%）
    // normX, normY: 文字左上角/中心/右上角的归一化坐标（取决于 align）
    void DrawText(FontHandle font,
                  std::string_view text,
                  float normX,
                  float normY,
                  float normFontSize,
                  Color color,
                  TextAlign align = TextAlign::Left);

    // 测量文字的归一化宽度（用来布局）
    float MeasureTextWidth(FontHandle font,
                           std::string_view text,
                           float normFontSize) const;

    // ── Sprite 渲染 ───────────────────────────────────────────────────────────

    // 简单贴图（整张纹理，可旋转）
    // rotation: 顺时针度数，0 = 不旋转
    void DrawSprite(TextureHandle texture,
                    NormRect dest,
                    float rotation = 0.0f,
                    Color tint     = Color::White,
                    float alpha    = 1.0f);

    // 带源矩形的贴图（精灵图集）
    void DrawSpriteEx(TextureHandle texture,
                      NormRect src,
                      NormRect dest,
                      float rotation = 0.0f,
                      Color tint     = Color::White,
                      float alpha    = 1.0f);

    // ── 几何图形（归一化坐标）────────────────────────────────────────────────

    // 实心圆，cx/cy = 圆心，normRadius = 半径（相对屏幕较短边）
    void DrawCircleFilled(float cx, float cy, float normRadius, Color color,
                          int segments = 64);

    // 圆形轮廓
    void DrawCircleOutline(float cx, float cy, float normRadius, Color color,
                           float normThickness = 0.002f, int segments = 64);

    // 直线（带厚度），x1/y1 → x2/y2 均为归一化坐标
    void DrawLine(float x1, float y1, float x2, float y2, Color color,
                  float normThickness = 0.002f);

    // 弧线，angles 单位：度（0 = 右方，顺时针）
    void DrawArc(float cx, float cy, float normRadius,
                 float startAngleDeg, float endAngleDeg,
                 Color color, float normThickness = 0.002f,
                 int segments = 64);

    // 圆角矩形
    void DrawRoundedRect(NormRect rect, float normCornerRadius,
                         Color color, bool filled = true,
                         int cornerSegments = 12,
                         float normThickness = 0.002f);

    // ── 混合模式 ──────────────────────────────────────────────────────────────
    void SetBlendMode(BlendMode mode);

    // ── 屏幕震动 viewport 偏移 ────────────────────────────────────────────────
    // 每帧在 Clear() 之前调用，设置渲染偏移（单位：像素）
    // 正值 => 内容向右/下移；负值 => 向左/上移
    void SetViewportShake(int pixelDx, int pixelDy);

    // 重置震动偏移（恢复全屏 viewport）
    void ResetViewportShake();

    // ── 屏幕信息 ──────────────────────────────────────────────────────────────
    int GetScreenWidth()  const;
    int GetScreenHeight() const;

    // ── 原生访问器 ────────────────────────────────────────────────────────────
    SDL_Renderer*  GetSDLRenderer() const { return m_renderer; }

    // 获取底层 GPU 设备（SDL 3.2+，可能为 nullptr）
    SDL_GPUDevice* GetGPUDevice()   const;

    bool IsValid() const { return m_renderer != nullptr; }

private:
    SDL_Renderer* m_renderer = nullptr;
    SDL_Window*   m_window   = nullptr;

    // 屏幕震动用 viewport 偏移（像素）
    int m_shakeOffsetX = 0;
    int m_shakeOffsetY = 0;
};

} // namespace sakura::core
