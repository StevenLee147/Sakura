#pragma once

// particle_system.h — 粒子系统（归一化坐标）
// 提供通用对象池 + 预设粒子配置，用于命中爆炸、樱花飘落、里程碑特效等。

#include "core/renderer.h"

#include <array>
#include <cstdint>
#include <functional>

namespace sakura::effects
{

// ============================================================================
// Particle — 单个粒子的运行时状态（全归一化，相对屏幕比例）
// ============================================================================
struct Particle
{
    float x         = 0.0f;   // 位置 x（归一化）
    float y         = 0.0f;   // 位置 y（归一化）
    float vx        = 0.0f;   // 速度 x（归一化/s）
    float vy        = 0.0f;   // 速度 y（归一化/s）
    float ax        = 0.0f;   // 加速度 x（归一化/s²）
    float ay        = 0.0f;   // 加速度 y（归一化/s²）
    float size      = 0.01f;  // 半径（归一化，相对短边）
    float sizeEnd   = 0.005f; // 结束时半径
    float rotation  = 0.0f;   // 当前旋转角度（度）
    float rotSpeed  = 0.0f;   // 旋转速度（度/s）
    float life      = 1.0f;   // 剩余生命（秒）
    float maxLife   = 1.0f;   // 最大生命（秒）
    sakura::core::Color colorStart   = sakura::core::Color::White;
    sakura::core::Color colorEnd     = sakura::core::Color{255, 255, 255, 0};
    bool  active    = false;
};

// ============================================================================
// ParticleConfig — 发射配置（描述一次发射的外观与物理属性）
// ============================================================================
struct ParticleConfig
{
    // 位置抖动（归一化，在目标位置附近随机偏移）
    float spreadX     = 0.02f;
    float spreadY     = 0.02f;

    // 速度范围（归一化/s），每轴独立随机
    float vxMin       = -0.1f;
    float vxMax       =  0.1f;
    float vyMin       = -0.2f;
    float vyMax       = -0.05f;

    // 加速度（归一化/s²）
    float ax          = 0.0f;
    float ay          = 0.05f;   // 重力

    // 大小
    float sizeMin     = 0.005f;
    float sizeMax     = 0.012f;
    float sizeEndMult = 0.3f;    // 结束时大小相对于初始大小的倍率

    // 旋转
    float rotSpeedMin = -180.0f;
    float rotSpeedMax =  180.0f;

    // 生存时间（秒）
    float lifeMin     = 0.3f;
    float lifeMax     = 0.8f;

    // 颜色（起始 / 结束，alpha 会线性衰减）
    sakura::core::Color colorStart   = { 255, 255, 255, 220 };
    sakura::core::Color colorEnd     = { 255, 255, 255,   0 };
};

// ── 预设配置构建函数 ──────────────────────────────────────────────────────────

namespace ParticlePresets
{
    // 樱花飘落（背景装饰，分层：前景/中景/远景）
    ParticleConfig SakuraPetalForeground();
    ParticleConfig SakuraPetalMidground();
    ParticleConfig SakuraPetalBackground();

    // 遗留兼容，底层调用 Midground
    ParticleConfig SakuraPetal();

    // 鼠标点击爆炸光点
    ParticleConfig ClickSpark();

    // 命中爆发（判定时短暂火花）
    ParticleConfig HitBurst(sakura::core::Color color);

    // 连击里程碑（向上喷射金色粒子）
    ParticleConfig ComboMilestone();

    // 背景微粒（极慢极小，低透明度）
    ParticleConfig BackgroundFloat();

    // 判定方向火花（根据判定颜色）
    ParticleConfig JudgeSpark(sakura::core::Color color);
}

// ============================================================================
// ContinuousEmitter — 持续发射器状态
// ============================================================================
struct ContinuousEmitter
{
    float x           = 0.5f;
    float y           = 0.5f;
    float rate        = 5.0f;   // 粒子/秒
    ParticleConfig cfg;
    float accumulator = 0.0f;   // 发射累计时间
    bool  active      = false;
};

// ============================================================================
// ParticleSystem — 粒子系统主类（对象池）
// ============================================================================
class ParticleSystem
{
public:
    static constexpr int MAX_PARTICLES   = 2000;
    static constexpr int MAX_EMITTERS    = 16;

    ParticleSystem();

    // ── 发射 ─────────────────────────────────────────────────────────────────

    // 立即在 (x, y) 发射 count 个粒子
    void Emit(float x, float y, int count, const ParticleConfig& cfg);

    // 注册持续发射器，返回 emitter id（-1=失败）
    int  EmitContinuous(float x, float y, float rate, const ParticleConfig& cfg);

    // 更新持续发射器位置
    void UpdateEmitterPos(int id, float x, float y);

    // 停止持续发射器
    void StopEmitter(int id);

    // ── 生命周期 ──────────────────────────────────────────────────────────────

    void Update(float dt);
    void Render(sakura::core::Renderer& renderer);
    void Clear();   // 清除所有活跃粒子

    // 获取当前活跃粒子数
    int GetActiveCount() const { return m_activeCount; }

private:
    std::array<Particle, MAX_PARTICLES>         m_pool;
    std::array<ContinuousEmitter, MAX_EMITTERS> m_emitters;
    int m_activeCount = 0;

    // 分配一个空闲粒子槽（返回指针，无槽则 nullptr）
    Particle* AllocParticle();

    // 用配置初始化一个粒子
    void InitParticle(Particle& p, float x, float y, const ParticleConfig& cfg);

    // 线性插值颜色
    static sakura::core::Color LerpColor(const sakura::core::Color& a,
                                          const sakura::core::Color& b,
                                          float t);
};

} // namespace sakura::effects
