#include "particle_system.h"
#include "core/config.h"
#include "core/resource_manager.h"
#include "core/theme.h"
#include "utils/logger.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace sakura::effects
{

// ============================================================================
// 随机数生成器（模块内静态）
// ============================================================================
static std::mt19937 s_rng{ std::random_device{}() };
static constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
static constexpr float kBlossomProbability = 0.30f;
struct BlossomUnitOffset
{
    float x;
    float y;
};

static constexpr std::array<BlossomUnitOffset, 5> kBlossomPetalOffsets = {{
    { 1.0000000f,  0.0000000f },
    { 0.3090170f,  0.9510565f },
    {-0.8090170f,  0.5877852f },
    {-0.8090170f, -0.5877852f },
    { 0.3090170f, -0.9510565f },
}};

static float RandFloat(float lo, float hi)
{
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(s_rng);
}

// ============================================================================
// 预设配置实现
// ============================================================================
namespace ParticlePresets
{

ParticleConfig SakuraPetalForeground()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.50f;
    cfg.spreadY      =  0.0f;
    cfg.vxMin        = -0.05f;
    cfg.vxMax        =  0.05f;
    cfg.vyMin        =  0.15f;  // 快速落下
    cfg.vyMax        =  0.25f;
    cfg.ax           =  0.01f;
    cfg.ay           =  0.03f;
    cfg.sizeMin      =  0.025f; // 大花瓣
    cfg.sizeMax      =  0.040f;
    cfg.sizeEndMult  =  0.9f;
    cfg.rotSpeedMin  = -180.0f;
    cfg.rotSpeedMax  =  180.0f;
    cfg.lifeMin      =  2.0f;
    cfg.lifeMax      =  3.5f;
    // 前景高斯模糊，这里用较低透明度和更深/明亮的颜色近似
    cfg.colorStart   = { 255, 160, 190, 160 };
    cfg.colorEnd     = { 255, 130, 160,   0 };
    cfg.shape        = ParticleShape::SakuraMix;
    return cfg;
}

ParticleConfig SakuraPetalMidground()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.50f;
    cfg.spreadY      =  0.0f;
    cfg.vxMin        = -0.02f;
    cfg.vxMax        =  0.02f;
    cfg.vyMin        =  0.05f;  // 正常速度
    cfg.vyMax        =  0.10f;
    cfg.ax           =  0.005f;
    cfg.ay           =  0.015f;
    cfg.sizeMin      =  0.010f;
    cfg.sizeMax      =  0.018f;
    cfg.sizeEndMult  =  0.8f;
    cfg.rotSpeedMin  = -90.0f;
    cfg.rotSpeedMax  =  90.0f;
    cfg.lifeMin      =  4.0f;
    cfg.lifeMax      =  6.0f;
    cfg.colorStart   = { 255, 180, 200, 220 };
    cfg.colorEnd     = { 255, 150, 180,   0 };
    cfg.shape        = ParticleShape::SakuraMix;
    return cfg;
}

ParticleConfig SakuraPetalBackground()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.50f;
    cfg.spreadY      =  0.0f;
    cfg.vxMin        = -0.01f;
    cfg.vxMax        =  0.01f;
    cfg.vyMin        =  0.01f;  // 慢速漂浮
    cfg.vyMax        =  0.03f;
    cfg.ax           =  0.002f;
    cfg.ay           =  0.005f;
    cfg.sizeMin      =  0.003f; // 极小花瓣
    cfg.sizeMax      =  0.008f;
    cfg.sizeEndMult  =  0.7f;
    cfg.rotSpeedMin  = -45.0f;
    cfg.rotSpeedMax  =  45.0f;
    cfg.lifeMin      =  8.0f;
    cfg.lifeMax      =  12.0f;
    cfg.colorStart   = { 255, 190, 210, 100 };
    cfg.colorEnd     = { 255, 170, 190,   0 };
    cfg.shape        = ParticleShape::SakuraMix;
    return cfg;
}

ParticleConfig SakuraPetal()
{
    return SakuraPetalMidground();
}

ParticleConfig ClickSpark()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.004f;
    cfg.spreadY      =  0.004f;
    cfg.vxMin        = -0.18f;
    cfg.vxMax        =  0.18f;
    cfg.vyMin        = -0.18f;
    cfg.vyMax        =  0.18f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.04f;
    cfg.sizeMin      =  0.006f;
    cfg.sizeMax      =  0.014f;
    cfg.sizeEndMult  =  0.15f;
    cfg.rotSpeedMin  = -220.0f;
    cfg.rotSpeedMax  =  220.0f;
    cfg.lifeMin      =  0.18f;
    cfg.lifeMax      =  0.36f;
    cfg.colorStart   = { 255, 236, 245, 255 };
    cfg.colorEnd     = { 246, 174, 204,   0 };
    cfg.shape        = ParticleShape::Starburst;
    return cfg;
}

ParticleConfig HitBurst(sakura::core::Color color)
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.012f;
    cfg.spreadY      =  0.012f;
    cfg.vxMin        = -0.34f;
    cfg.vxMax        =  0.34f;
    cfg.vyMin        = -0.44f;
    cfg.vyMax        = -0.06f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.34f;
    cfg.sizeMin      =  0.007f;
    cfg.sizeMax      =  0.017f;
    cfg.sizeEndMult  =  0.12f;
    cfg.rotSpeedMin  = -300.0f;
    cfg.rotSpeedMax  =  300.0f;
    cfg.lifeMin      =  0.20f;
    cfg.lifeMax      =  0.48f;
    cfg.colorStart   = {
        static_cast<uint8_t>(std::min(255, color.r + 35)),
        static_cast<uint8_t>(std::min(255, color.g + 35)),
        static_cast<uint8_t>(std::min(255, color.b + 35)),
        255
    };
    cfg.colorEnd     = { color.r, color.g, color.b, 0 };
    cfg.shape        = ParticleShape::Starburst;
    return cfg;
}

ParticleConfig ShockwaveRipple(sakura::core::Color color, float startRadius, float endMult)
{
    ParticleConfig cfg;
    cfg.spreadX      = 0.0f;
    cfg.spreadY      = 0.0f;
    cfg.vxMin        = 0.0f;
    cfg.vxMax        = 0.0f;
    cfg.vyMin        = 0.0f;
    cfg.vyMax        = 0.0f;
    cfg.ax           = 0.0f;
    cfg.ay           = 0.0f;
    cfg.sizeMin      = startRadius;
    cfg.sizeMax      = startRadius * 1.04f;
    cfg.sizeEndMult  = endMult;
    cfg.rotSpeedMin  = -35.0f;
    cfg.rotSpeedMax  =  35.0f;
    cfg.lifeMin      = 0.26f;
    cfg.lifeMax      = 0.34f;
    cfg.colorStart   = { color.r, color.g, color.b, 235 };
    cfg.colorEnd     = { color.r, color.g, color.b, 0 };
    cfg.shape        = ParticleShape::ShockwaveRing;
    return cfg;
}

ParticleConfig ComboMilestone()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.06f;
    cfg.spreadY      =  0.02f;
    cfg.vxMin        = -0.18f;
    cfg.vxMax        =  0.18f;
    cfg.vyMin        = -0.52f;
    cfg.vyMax        = -0.20f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.38f;
    cfg.sizeMin      =  0.010f;
    cfg.sizeMax      =  0.022f;
    cfg.sizeEndMult  =  0.15f;
    cfg.rotSpeedMin  = -180.0f;
    cfg.rotSpeedMax  =  180.0f;
    cfg.lifeMin      =  0.75f;
    cfg.lifeMax      =  1.35f;
    cfg.colorStart   = { 255, 228, 152, 250 };  // 流光暖金
    cfg.colorEnd     = { 255, 175, 198,   0 };
    cfg.shape        = ParticleShape::Starburst;
    return cfg;
}

ParticleConfig BackgroundFloat()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.95f;
    cfg.spreadY      =  0.95f;
    cfg.vxMin        = -0.007f;
    cfg.vxMax        =  0.007f;
    cfg.vyMin        = -0.014f;
    cfg.vyMax        = -0.002f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.0f;
    cfg.sizeMin      =  0.008f;
    cfg.sizeMax      =  0.026f;
    cfg.sizeEndMult  =  1.15f;
    cfg.rotSpeedMin  = -15.0f;
    cfg.rotSpeedMax  =  15.0f;
    cfg.lifeMin      =  4.5f;
    cfg.lifeMax      =  9.0f;
    cfg.colorStart   = { 235, 198, 224, 58 };
    cfg.colorEnd     = { 172, 206, 240,  0 };
    cfg.shape        = ParticleShape::Bokeh;
    return cfg;
}

ParticleConfig JudgeSpark(sakura::core::Color color)
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.022f;
    cfg.spreadY      =  0.010f;
    cfg.vxMin        = -0.26f;
    cfg.vxMax        =  0.26f;
    cfg.vyMin        = -0.28f;
    cfg.vyMax        = -0.04f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.22f;
    cfg.sizeMin      =  0.006f;
    cfg.sizeMax      =  0.013f;
    cfg.sizeEndMult  =  0.12f;
    cfg.rotSpeedMin  = -240.0f;
    cfg.rotSpeedMax  =  240.0f;
    cfg.lifeMin      =  0.16f;
    cfg.lifeMax      =  0.38f;
    cfg.colorStart   = color;
    cfg.colorEnd     = { color.r, color.g, color.b, 0 };
    cfg.shape        = ParticleShape::Streak;
    return cfg;
}

} // namespace ParticlePresets

static void DrawSakuraBlossom(sakura::core::Renderer& renderer, const Particle& p,
                              float sz, const sakura::core::Color& col)
{
    static constexpr float kBlossomPetalRadiusRatio = 0.58f;
    static constexpr float kBlossomPetalOffsetRatio = 0.68f;
    static constexpr float kBlossomVerticalSquash = 0.82f;
    static constexpr float kBlossomCoreRadiusRatio = 0.36f;
    const float petalR = sz * kBlossomPetalRadiusRatio;
    const float offset = sz * kBlossomPetalOffsetRatio;
    const float baseRad = p.rotation * kDegToRad;
    const float baseCos = std::cos(baseRad);
    const float baseSin = std::sin(baseRad);
    for (const auto& unit : kBlossomPetalOffsets)
    {
        const float rotatedX = baseCos * unit.x - baseSin * unit.y;
        const float rotatedY = baseSin * unit.x + baseCos * unit.y;
        renderer.DrawCircleFilled(p.x + rotatedX * offset,
                                  p.y + rotatedY * offset * kBlossomVerticalSquash,
                                  petalR, col, 10);
    }
    renderer.DrawCircleFilled(p.x, p.y, sz * kBlossomCoreRadiusRatio,
                              { 255, 240, 200, col.a }, 8);
}

namespace
{
struct ParticleSpriteSet
{
    sakura::core::TextureHandle radialGlow   = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle streakGlow   = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle bokehSoft    = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle starburst    = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle ringRipple   = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle petalPrimary = sakura::core::INVALID_HANDLE;
    sakura::core::TextureHandle petalSlender = sakura::core::INVALID_HANDLE;
};

sakura::core::TextureHandle EnsureParticleSprite(sakura::core::TextureHandle& slot, const char* path)
{
    auto& rm = sakura::core::ResourceManager::GetInstance();
    if (slot == sakura::core::INVALID_HANDLE || rm.GetTexture(slot) == nullptr)
    {
        auto loaded = rm.LoadTexture(path);
        slot = loaded.value_or(sakura::core::INVALID_HANDLE);
    }
    return slot;
}

const ParticleSpriteSet& GetParticleSprites()
{
    static ParticleSpriteSet s_set;
    EnsureParticleSprite(s_set.radialGlow,   "resources/images/particles/glow_radial.png");
    EnsureParticleSprite(s_set.streakGlow,   "resources/images/particles/glow_streak.png");
    EnsureParticleSprite(s_set.bokehSoft,    "resources/images/particles/bokeh_soft.png");
    EnsureParticleSprite(s_set.starburst,    "resources/images/particles/starburst.png");
    EnsureParticleSprite(s_set.ringRipple,   "resources/images/particles/ring_ripple.png");
    EnsureParticleSprite(s_set.petalPrimary, "resources/images/particles/petal_primary.png");
    EnsureParticleSprite(s_set.petalSlender, "resources/images/particles/petal_slender.png");
    return s_set;
}

void DrawOrientedSprite(sakura::core::Renderer& renderer,
                        sakura::core::TextureHandle handle,
                        float cx, float cy,
                        float halfWMinDim, float halfHMinDim,
                        float rotRad,
                        sakura::core::Color col)
{
    const int sw = std::max(1, renderer.GetScreenWidth());
    const int sh = std::max(1, renderer.GetScreenHeight());
    const float minDim = static_cast<float>(std::min(sw, sh));
    const float invW = 1.0f / static_cast<float>(sw);
    const float invH = 1.0f / static_cast<float>(sh);

    const float pxX = cx * static_cast<float>(sw);
    const float pxY = cy * static_cast<float>(sh);
    const float hw = halfWMinDim * minDim;
    const float hh = halfHMinDim * minDim;

    const float c = std::cos(rotRad);
    const float s = std::sin(rotRad);
    const float ux =  c * hw;
    const float uy =  s * hw;
    const float vx = -s * hh;
    const float vy =  c * hh;

    const sakura::core::NormPoint p0{ (pxX - ux - vx) * invW, (pxY - uy - vy) * invH };
    const sakura::core::NormPoint p1{ (pxX + ux - vx) * invW, (pxY + uy - vy) * invH };
    const sakura::core::NormPoint p2{ (pxX + ux + vx) * invW, (pxY + uy + vy) * invH };
    const sakura::core::NormPoint p3{ (pxX - ux + vx) * invW, (pxY - uy + vy) * invH };

    renderer.DrawSpriteQuad(handle, p0, p1, p2, p3, col);
}
} // namespace

static void DrawSakuraPetal(sakura::core::Renderer& renderer, const Particle& p,
                            float sz, const sakura::core::Color& col,
                            const ParticleSpriteSet& sprites)
{
    const auto tex = (p.shapeSeed < 0.62f) ? sprites.petalPrimary : sprites.petalSlender;
    const float rotRad = p.rotation * kDegToRad;
    // 3D tumble foreshortening: minor axis breathes between 0.30 and 1.0
    const float tumble = 0.30f + 0.70f * std::abs(std::cos(p.flutterPhase));
    if (tex != sakura::core::INVALID_HANDLE)
    {
        DrawOrientedSprite(renderer, tex, p.x, p.y, sz * 1.15f, sz * 1.15f * tumble, rotRad, col);
    }
    else
    {
        renderer.DrawPetal(p.x, p.y, sz, rotRad, col);
    }
}

// ============================================================================
// ParticleSystem 实现
// ============================================================================

ParticleSystem::ParticleSystem()
{
    // 全部初始化为非活跃
    for (auto& p : m_pool)
        p.active = false;
    for (auto& e : m_emitters)
        e.active = false;
}

// ── 内部辅助 ─────────────────────────────────────────────────────────────────

Particle* ParticleSystem::AllocParticle()
{
    for (auto& p : m_pool)
    {
        if (!p.active)
            return &p;
    }
    return nullptr;   // 池已满
}

void ParticleSystem::InitParticle(Particle& p, float x, float y,
                                   const ParticleConfig& cfg)
{
    p.x          = x + RandFloat(-cfg.spreadX * 0.5f, cfg.spreadX * 0.5f);
    p.y          = y + RandFloat(-cfg.spreadY * 0.5f, cfg.spreadY * 0.5f);
    p.vx         = RandFloat(cfg.vxMin, cfg.vxMax);
    p.vy         = RandFloat(cfg.vyMin, cfg.vyMax);
    p.ax         = cfg.ax;
    p.ay         = cfg.ay;
    p.rotation     = RandFloat(0.0f, 360.0f);
    p.rotSpeed     = RandFloat(cfg.rotSpeedMin, cfg.rotSpeedMax);
    p.flutterPhase = RandFloat(0.0f, 6.2831853f);
    p.flutterSpeed = RandFloat(cfg.flutterMin, cfg.flutterMax);
    float sz       = RandFloat(cfg.sizeMin, cfg.sizeMax);
    p.size         = sz;
    p.sizeEnd      = sz * cfg.sizeEndMult;
    p.maxLife      = RandFloat(cfg.lifeMin, cfg.lifeMax);
    p.life         = p.maxLife;
    p.shapeSeed    = RandFloat(0.0f, 1.0f);
    p.shape        = cfg.shape;

    p.colorStart = cfg.colorStart;
    p.colorEnd   = cfg.colorEnd;
    p.active     = true;
}

sakura::core::Color ParticleSystem::LerpColor(const sakura::core::Color& a,
                                               const sakura::core::Color& b,
                                               float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t),
    };
}

// ── 公开 API ─────────────────────────────────────────────────────────────────

void ParticleSystem::Emit(float x, float y, int count, const ParticleConfig& cfg)
{
    if(!sakura::core::Theme::GetInstance().Settings().particlesEnabled)return;
    count=static_cast<int>(std::round(count*sakura::core::Config::GetInstance().Get<float>("graphics.effect_intensity",0.7f)));
    for (int i = 0; i < count; ++i)
    {
        Particle* p = AllocParticle();
        if (!p) break;   // 池已满，停止
        InitParticle(*p, x, y, cfg);
    }
}

int ParticleSystem::EmitContinuous(float x, float y, float rate,
                                    const ParticleConfig& cfg)
{
    for (int i = 0; i < MAX_EMITTERS; ++i)
    {
        if (!m_emitters[i].active)
        {
            m_emitters[i] = ContinuousEmitter{ x, y, rate, cfg, 0.0f, true };
            return i;
        }
    }
    LOG_WARN("ParticleSystem::EmitContinuous: 发射器池已满");
    return -1;
}

void ParticleSystem::UpdateEmitterPos(int id, float x, float y)
{
    if (id < 0 || id >= MAX_EMITTERS) return;
    m_emitters[id].x = x;
    m_emitters[id].y = y;
}

void ParticleSystem::StopEmitter(int id)
{
    if (id < 0 || id >= MAX_EMITTERS) return;
    m_emitters[id].active = false;
}

void ParticleSystem::Update(float dt)
{
    const auto& settings=sakura::core::Config::GetInstance();
    if(!sakura::core::Theme::GetInstance().Settings().particlesEnabled || settings.Get<bool>("graphics.reduced_motion",false))return;
    // 处理持续发射器
    for (auto& em : m_emitters)
    {
        if (!em.active) continue;
        em.accumulator += dt;
        const float intensity=settings.Get<float>("graphics.effect_intensity",0.7f);
        if(em.rate<=0 || intensity<=0)continue;
        float interval = 1.0f / (em.rate*intensity);
        while (em.accumulator >= interval)
        {
            em.accumulator -= interval;
            Particle* p = AllocParticle();
            if (!p) break;
            InitParticle(*p, em.x, em.y, em.cfg);
        }
    }

    // 更新所有粒子
    m_activeCount = 0;
    for (auto& p : m_pool)
    {
        if (!p.active) continue;

        p.life -= dt;
        if (p.life <= 0.0f)
        {
            p.active = false;
            continue;
        }

        p.flutterPhase += p.flutterSpeed * dt;
        p.vx += p.ax * dt;
        p.vy += p.ay * dt;
        const float swayX = (p.shape == ParticleShape::SakuraMix)
            ? std::sin(p.flutterPhase) * 0.016f
            : 0.0f;
        p.x  += (p.vx + swayX) * dt;
        p.y  += p.vy * dt;
        p.rotation += p.rotSpeed * dt;

        ++m_activeCount;
    }
}

void ParticleSystem::Render(sakura::core::Renderer& renderer)
{
    if (!sakura::core::Theme::GetInstance().Settings().particlesEnabled ||
        sakura::core::Config::GetInstance().Get<bool>("graphics.reduced_motion", false))
        return;

    const auto& sprites = GetParticleSprites();

    // Pass 1: Alpha-blended physical sakura petals & blossoms for silky pastel translucency
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
    for (const auto& p : m_pool)
    {
        if (!p.active || p.shape != ParticleShape::SakuraMix) continue;

        const float t = 1.0f - (p.life / p.maxLife);
        // Smooth ease-in-out alpha envelope
        const float fadeIn = std::clamp(t * 6.0f, 0.0f, 1.0f);
        sakura::core::Color col = LerpColor(p.colorStart, p.colorEnd, t);
        col.a = static_cast<uint8_t>(col.a * fadeIn);
        const float sz = p.size + (p.sizeEnd - p.size) * t;

        if (p.shapeSeed < kBlossomProbability * 0.65f)
            DrawSakuraBlossom(renderer, p, sz, col);
        else
            DrawSakuraPetal(renderer, p, sz, col, sprites);
    }

    // Pass 2: Additive optical light sprites (glows, starbursts, bokeh, streaks, shockwaves)
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    for (const auto& p : m_pool)
    {
        if (!p.active || p.shape == ParticleShape::SakuraMix) continue;

        const float t = 1.0f - (p.life / p.maxLife);
        // Cubic ease-out expansion for shockwave rings, smooth linear for sparks
        const float sizeT = (p.shape == ParticleShape::ShockwaveRing)
            ? (1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t))
            : t;
        sakura::core::Color col = LerpColor(p.colorStart, p.colorEnd, t);
        if (p.shape == ParticleShape::Bokeh)
        {
            const float bell = std::sin(std::clamp(t, 0.0f, 1.0f) * std::numbers::pi_v<float>);
            col.a = static_cast<uint8_t>(p.colorStart.a * bell);
        }
        const float sz = std::max(0.0005f, p.size + (p.sizeEnd - p.size) * sizeT);
        const float rotRad = p.rotation * kDegToRad;

        switch (p.shape)
        {
        case ParticleShape::Starburst:
            if (sprites.starburst != sakura::core::INVALID_HANDLE)
                DrawOrientedSprite(renderer, (p.shapeSeed < 0.65f) ? sprites.starburst : sprites.radialGlow, p.x, p.y, sz * 1.4f, sz * 1.4f, rotRad, col);
            else
                renderer.DrawRadialGradient(p.x, p.y, sz, sz, col, { col.r, col.g, col.b, 0 }, 16);
            break;
        case ParticleShape::Bokeh:
            if (sprites.bokehSoft != sakura::core::INVALID_HANDLE)
                DrawOrientedSprite(renderer, sprites.bokehSoft, p.x, p.y, sz, sz, rotRad, col);
            else
                renderer.DrawRadialGradient(p.x, p.y, sz, sz, col, { col.r, col.g, col.b, 0 }, 20);
            break;
        case ParticleShape::Streak:
        {
            const float speedAngle = (std::abs(p.vx) + std::abs(p.vy) > 0.001f)
                ? std::atan2(p.vy, p.vx)
                : rotRad;
            if (sprites.streakGlow != sakura::core::INVALID_HANDLE)
                DrawOrientedSprite(renderer, sprites.streakGlow, p.x, p.y, sz * 2.4f, sz * 0.65f, speedAngle, col);
            else
                renderer.DrawCircleFilled(p.x, p.y, sz, col, 10);
            break;
        }
        case ParticleShape::ShockwaveRing:
            if (sprites.ringRipple != sakura::core::INVALID_HANDLE)
                DrawOrientedSprite(renderer, sprites.ringRipple, p.x, p.y, sz, sz, rotRad, col);
            else
                renderer.DrawRingGradient(p.x, p.y, sz * 0.80f, sz, col, { col.r, col.g, col.b, 0 }, 48);
            break;
        case ParticleShape::RadialGlow:
        case ParticleShape::Circle:
        default:
            if (sprites.radialGlow != sakura::core::INVALID_HANDLE)
                DrawOrientedSprite(renderer, sprites.radialGlow, p.x, p.y, sz * 1.25f, sz * 1.25f, 0.0f, col);
            else
                renderer.DrawCircleFilled(p.x, p.y, sz, col, 12);
            break;
        }
    }

    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
}

void ParticleSystem::Clear()
{
    for (auto& p : m_pool)
        p.active = false;
    m_activeCount = 0;
}

} // namespace sakura::effects
