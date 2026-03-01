#include "particle_system.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sakura::effects
{

// ============================================================================
// 随机数生成器（模块内静态）
// ============================================================================
static std::mt19937 s_rng{ std::random_device{}() };

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
    return cfg;
}

ParticleConfig SakuraPetal()
{
    return SakuraPetalMidground();
}

ParticleConfig ClickSpark()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.0f;
    cfg.spreadY      =  0.0f;
    cfg.vxMin        = -0.15f;
    cfg.vxMax        =  0.15f;
    cfg.vyMin        = -0.15f;
    cfg.vyMax        =  0.15f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.0f;  
    cfg.sizeMin      =  0.003f;
    cfg.sizeMax      =  0.008f;
    cfg.sizeEndMult  =  0.1f;
    cfg.rotSpeedMin  = -180.0f;
    cfg.rotSpeedMax  =  180.0f;
    cfg.lifeMin      =  0.15f;
    cfg.lifeMax      =  0.3f;
    cfg.colorStart   = { 255, 255, 255, 255 };
    cfg.colorEnd     = { 255, 200, 255,   0 };
    return cfg;
}

ParticleConfig HitBurst(sakura::core::Color color)
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.01f;
    cfg.spreadY      =  0.01f;
    cfg.vxMin        = -0.30f;
    cfg.vxMax        =  0.30f;
    cfg.vyMin        = -0.40f;
    cfg.vyMax        = -0.10f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.30f;  // 弧形重力
    cfg.sizeMin      =  0.003f;
    cfg.sizeMax      =  0.008f;
    cfg.sizeEndMult  =  0.1f;
    cfg.rotSpeedMin  = -360.0f;
    cfg.rotSpeedMax  =  360.0f;
    cfg.lifeMin      =  0.18f;
    cfg.lifeMax      =  0.45f;
    cfg.colorStart   = color;
    cfg.colorEnd     = { color.r, color.g, color.b, 0 };
    return cfg;
}

ParticleConfig ComboMilestone()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.05f;
    cfg.spreadY      =  0.02f;
    cfg.vxMin        = -0.15f;
    cfg.vxMax        =  0.15f;
    cfg.vyMin        = -0.50f;
    cfg.vyMax        = -0.20f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.40f;  // 重力拉回
    cfg.sizeMin      =  0.005f;
    cfg.sizeMax      =  0.012f;
    cfg.sizeEndMult  =  0.1f;
    cfg.rotSpeedMin  = -180.0f;
    cfg.rotSpeedMax  =  180.0f;
    cfg.lifeMin      =  0.8f;
    cfg.lifeMax      =  1.4f;
    cfg.colorStart   = { 255, 215,   0, 240 };  // 金色
    cfg.colorEnd     = { 255, 200,  50,   0 };
    return cfg;
}

ParticleConfig BackgroundFloat()
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.5f;
    cfg.spreadY      =  0.5f;
    cfg.vxMin        = -0.005f;
    cfg.vxMax        =  0.005f;
    cfg.vyMin        = -0.01f;
    cfg.vyMax        =  0.005f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.0f;
    cfg.sizeMin      =  0.001f;
    cfg.sizeMax      =  0.003f;
    cfg.sizeEndMult  =  0.5f;
    cfg.rotSpeedMin  =  0.0f;
    cfg.rotSpeedMax  =  0.0f;
    cfg.lifeMin      =  3.0f;
    cfg.lifeMax      =  7.0f;
    cfg.colorStart   = { 200, 200, 255,  50 };
    cfg.colorEnd     = { 200, 200, 255,   0 };
    return cfg;
}

ParticleConfig JudgeSpark(sakura::core::Color color)
{
    ParticleConfig cfg;
    cfg.spreadX      =  0.02f;
    cfg.spreadY      =  0.01f;
    cfg.vxMin        = -0.20f;
    cfg.vxMax        =  0.20f;
    cfg.vyMin        = -0.25f;
    cfg.vyMax        = -0.05f;
    cfg.ax           =  0.0f;
    cfg.ay           =  0.20f;
    cfg.sizeMin      =  0.002f;
    cfg.sizeMax      =  0.006f;
    cfg.sizeEndMult  =  0.05f;
    cfg.rotSpeedMin  = -270.0f;
    cfg.rotSpeedMax  =  270.0f;
    cfg.lifeMin      =  0.15f;
    cfg.lifeMax      =  0.40f;
    cfg.colorStart   = color;
    cfg.colorEnd     = { color.r, color.g, color.b, 0 };
    return cfg;
}

} // namespace ParticlePresets

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
    p.rotation   = RandFloat(0.0f, 360.0f);
    p.rotSpeed   = RandFloat(cfg.rotSpeedMin, cfg.rotSpeedMax);

    float sz     = RandFloat(cfg.sizeMin, cfg.sizeMax);
    p.size       = sz;
    p.sizeEnd    = sz * cfg.sizeEndMult;

    p.maxLife    = RandFloat(cfg.lifeMin, cfg.lifeMax);
    p.life       = p.maxLife;

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
    // 处理持续发射器
    for (auto& em : m_emitters)
    {
        if (!em.active) continue;
        em.accumulator += dt;
        float interval = 1.0f / em.rate;
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

        p.vx += p.ax * dt;
        p.vy += p.ay * dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.rotation += p.rotSpeed * dt;

        ++m_activeCount;
    }
}

void ParticleSystem::Render(sakura::core::Renderer& renderer)
{
    renderer.SetBlendMode(sakura::core::BlendMode::Additive);

    for (const auto& p : m_pool)
    {
        if (!p.active) continue;

        float t = 1.0f - (p.life / p.maxLife);   // 0=刚出生, 1=即死
        sakura::core::Color col = LerpColor(p.colorStart, p.colorEnd, t);
        float sz = p.size + (p.sizeEnd - p.size) * t;

        renderer.DrawCircleFilled(p.x, p.y, sz, col, 8);
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
