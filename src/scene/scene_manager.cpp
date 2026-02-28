#include "scene_manager.h"
#include "utils/logger.h"
#include "core/renderer.h"

#include <cmath>
#include <algorithm>

namespace sakura::scene
{

SceneManager::SceneManager() = default;

SceneManager::~SceneManager()
{
    // 退出时逐层弹栈，调 OnExit
    while (!m_sceneStack.empty())
    {
        m_sceneStack.back()->OnExit();
        m_sceneStack.pop_back();
    }

    if (m_texFrom)
    {
        SDL_DestroyTexture(m_texFrom);
        m_texFrom = nullptr;
    }
    if (m_texTo)
    {
        SDL_DestroyTexture(m_texTo);
        m_texTo = nullptr;
    }
}

// ── 场景获取 ──────────────────────────────────────────────────────────────────

Scene* SceneManager::GetCurrentScene() const
{
    if (m_sceneStack.empty()) return nullptr;
    return m_sceneStack.back().get();
}

// ── 场景切换请求 ──────────────────────────────────────────────────────────────

void SceneManager::SwitchScene(std::unique_ptr<Scene> newScene,
                                TransitionType transition,
                                float durationSec)
{
    // 任何时候都可以请求切换，但等当前帧末尾才真正执行
    m_pendingScene    = std::move(newScene);
    m_pendingIsPush   = false;
    m_pendingIsPop    = false;
    m_transitionType  = transition;
    m_transitionDuration = durationSec;
    LOG_DEBUG("SceneManager::SwitchScene 已请求 (transition={})",
        static_cast<int>(transition));
}

void SceneManager::PushScene(std::unique_ptr<Scene> newScene,
                              TransitionType transition,
                              float durationSec)
{
    m_pendingScene    = std::move(newScene);
    m_pendingIsPush   = true;
    m_pendingIsPop    = false;
    m_transitionType  = transition;
    m_transitionDuration = durationSec;
    LOG_DEBUG("SceneManager::PushScene 已请求");
}

void SceneManager::PopScene(TransitionType transition, float durationSec)
{
    if (m_sceneStack.size() <= 1)
    {
        LOG_WARN("SceneManager::PopScene: 栈中只剩一个场景，无法弹出");
        return;
    }
    m_pendingIsPop    = true;
    m_pendingIsPush   = false;
    m_pendingScene    = nullptr;
    m_transitionType  = transition;
    m_transitionDuration = durationSec;
    LOG_DEBUG("SceneManager::PopScene 已请求");
}

// ── 主循环委托 ────────────────────────────────────────────────────────────────

void SceneManager::Update(float dt)
{
    if (m_isTransitioning)
    {
        m_transitionTimer += dt;

        if (m_transitionTimer >= m_transitionDuration)
        {
            // 过渡结束——应用待切换场景
            m_isTransitioning = false;
            ApplyPendingSwitch();
        }
        else
        {
            // 过渡进行中，更新新场景（如果有）
            if (!m_sceneStack.empty())
            {
                m_sceneStack.back()->OnUpdate(dt);
            }
            return;
        }
    }

    // 检查是否有待处理的场景切换（无过渡动画立即执行，有过渡则开始录制快照）
    if (m_pendingScene || m_pendingIsPop)
    {
        if (m_transitionType == TransitionType::None)
        {
            // 无过渡：立即切换
            float noTrans = 0.0f;
            (void)noTrans;
            ApplyPendingSwitch();
        }
        else
        {
            // 开始过渡（需要在 Render 阶段录制快照纹理）
            m_isTransitioning  = true;
            m_transitionTimer  = 0.0f;
            // 实际快照在 Render 中第一帧录制
        }
        return;
    }

    // 正常更新
    if (!m_sceneStack.empty())
    {
        m_sceneStack.back()->OnUpdate(dt);
    }
}

void SceneManager::Render(sakura::core::Renderer& renderer)
{
    if (m_sceneStack.empty()) return;

    if (m_isTransitioning)
    {
        RenderTransition(renderer);
        return;
    }

    // 正常渲染（从栈底到栈顶，透明场景会显示下层内容）
    for (auto& scene : m_sceneStack)
    {
        if (scene.get() == m_sceneStack.back().get() || scene->IsTransparent())
        {
            scene->OnRender(renderer);
        }
    }
}

void SceneManager::HandleEvent(const SDL_Event& event)
{
    if (m_isTransitioning) return;  // 过渡中忽略事件

    if (!m_sceneStack.empty())
    {
        m_sceneStack.back()->OnEvent(event);
    }
}

// ── 应用待切换场景 ────────────────────────────────────────────────────────────

void SceneManager::ApplyPendingSwitch()
{
    if (m_pendingIsPop)
    {
        // 弹出栈顶
        if (!m_sceneStack.empty())
        {
            m_sceneStack.back()->OnExit();
            m_sceneStack.pop_back();
        }
        m_pendingIsPop = false;
        if (!m_sceneStack.empty())
        {
            m_sceneStack.back()->OnEnter();
        }
        LOG_DEBUG("SceneManager: 场景已弹出，栈深={}", m_sceneStack.size());
        return;
    }

    if (!m_pendingScene) return;

    if (m_pendingIsPush)
    {
        // 压入新场景（保留当前）
        if (!m_sceneStack.empty())
        {
            // 当前场景不调 OnExit（只是被覆盖）
        }
        m_sceneStack.push_back(std::move(m_pendingScene));
        m_sceneStack.back()->OnEnter();

        m_pendingIsPush = false;
        LOG_DEBUG("SceneManager: 场景已压栈，栈深={}", m_sceneStack.size());
    }
    else
    {
        // 替换当前（清空栈并放入新场景）
        while (!m_sceneStack.empty())
        {
            m_sceneStack.back()->OnExit();
            m_sceneStack.pop_back();
        }
        m_sceneStack.push_back(std::move(m_pendingScene));
        m_sceneStack.back()->OnEnter();

        LOG_DEBUG("SceneManager: 场景已切换，栈深={}", m_sceneStack.size());
    }

    // 清理离屏快照
    if (m_texFrom)
    {
        SDL_DestroyTexture(m_texFrom);
        m_texFrom = nullptr;
    }
    if (m_texTo)
    {
        SDL_DestroyTexture(m_texTo);
        m_texTo = nullptr;
    }
}

// ── 过渡渲染 ──────────────────────────────────────────────────────────────────

void SceneManager::RenderTransition(sakura::core::Renderer& renderer)
{
    SDL_Renderer* sdlRenderer = renderer.GetSDLRenderer();
    const int sw = renderer.GetScreenWidth();
    const int sh = renderer.GetScreenHeight();
    const float t = std::clamp(m_transitionTimer / m_transitionDuration, 0.0f, 1.0f);

    // 首帧：录制源场景快照
    if (!m_texFrom && !m_sceneStack.empty())
    {
        // 在过渡中，当前栈顶还是"旧"场景（pending 尚未应用）
        // 创建离屏纹理保存源快照
        m_texFrom = SDL_CreateTexture(sdlRenderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET,
            sw, sh);

        if (m_texFrom)
        {
            SDL_SetRenderTarget(sdlRenderer, m_texFrom);
            SDL_SetRenderDrawColor(sdlRenderer, 15, 15, 35, 255);
            SDL_RenderClear(sdlRenderer);

            // 渲染旧场景到快照
            for (auto& scene : m_sceneStack)
            {
                scene->OnRender(renderer);
            }

            SDL_SetRenderTarget(sdlRenderer, nullptr);
        }
    }

    // 根据过渡类型渲染
    switch (m_transitionType)
    {
        case TransitionType::Fade:
        {
            // 先画源场景（使用快照 texFrom）
            if (m_texFrom)
            {
                SDL_SetTextureAlphaMod(m_texFrom, static_cast<uint8_t>((1.0f - t) * 255));
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, nullptr);
            }
            else
            {
                // 没有快照时直接渲染旧场景
                for (auto& scene : m_sceneStack)
                {
                    scene->OnRender(renderer);
                }
            }

            // 叠加一层逐渐加深的过渡遮罩
            uint8_t alpha = static_cast<uint8_t>(t * 255);
            SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, alpha);
            SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(sdlRenderer, nullptr);
            break;
        }

        case TransitionType::SlideLeft:
        {
            // 源场景向左滑出，目标场景从右滑入
            if (m_texFrom)
            {
                float offsetX = -t * static_cast<float>(sw);
                SDL_FRect srcDest = { offsetX, 0.0f, static_cast<float>(sw), static_cast<float>(sh) };
                SDL_SetTextureAlphaMod(m_texFrom, 255);
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, &srcDest);
            }

            // 新场景从右側预渲染（简化：显示黑色占位 + 进度条）
            float newX = static_cast<float>(sw) * (1.0f - t);
            SDL_SetRenderDrawColor(sdlRenderer, 15, 15, 35, 255);
            SDL_FRect newDest = { newX, 0.0f, static_cast<float>(sw), static_cast<float>(sh) };
            SDL_RenderFillRect(sdlRenderer, &newDest);
            break;
        }

        case TransitionType::SlideRight:
        {
            if (m_texFrom)
            {
                float offsetX = t * static_cast<float>(sw);
                SDL_FRect srcDest = { offsetX, 0.0f, static_cast<float>(sw), static_cast<float>(sh) };
                SDL_SetTextureAlphaMod(m_texFrom, 255);
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, &srcDest);
            }
            float newX = -(static_cast<float>(sw) * (1.0f - t));
            SDL_SetRenderDrawColor(sdlRenderer, 15, 15, 35, 255);
            SDL_FRect newDest = { newX, 0.0f, static_cast<float>(sw), static_cast<float>(sh) };
            SDL_RenderFillRect(sdlRenderer, &newDest);
            break;
        }

        case TransitionType::SlideUp:
        {
            if (m_texFrom)
            {
                float offsetY = -t * static_cast<float>(sh);
                SDL_FRect srcDest = { 0.0f, offsetY, static_cast<float>(sw), static_cast<float>(sh) };
                SDL_SetTextureAlphaMod(m_texFrom, 255);
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, &srcDest);
            }
            break;
        }

        case TransitionType::SlideDown:
        {
            if (m_texFrom)
            {
                float offsetY = t * static_cast<float>(sh);
                SDL_FRect srcDest = { 0.0f, offsetY, static_cast<float>(sw), static_cast<float>(sh) };
                SDL_SetTextureAlphaMod(m_texFrom, 255);
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, &srcDest);
            }
            break;
        }

        case TransitionType::Scale:
        {
            // 新场景从中心缩放出现
            if (m_texFrom)
            {
                SDL_SetTextureAlphaMod(m_texFrom, static_cast<uint8_t>((1.0f - t) * 255));
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, nullptr);
            }
            // 叠加一层从中心扩展的矩形（简化 Scale 效果）
            float scale     = t;
            float cx        = static_cast<float>(sw) * 0.5f;
            float cy        = static_cast<float>(sh) * 0.5f;
            float halfW     = cx * scale;
            float halfH     = cy * scale;
            SDL_SetRenderDrawColor(sdlRenderer, 15, 15, 35, 255);
            SDL_FRect scaleDest = { cx - halfW, cy - halfH, halfW * 2.0f, halfH * 2.0f };
            SDL_RenderFillRect(sdlRenderer, &scaleDest);
            break;
        }

        case TransitionType::CircleWipe:
        {
            // 简化圆形遮罩（用渐变遮罩替代）
            if (m_texFrom)
            {
                SDL_SetTextureAlphaMod(m_texFrom, 255);
                SDL_RenderTexture(sdlRenderer, m_texFrom, nullptr, nullptr);
            }
            // 叠加带透明度的遮罩
            renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
                sakura::core::Color(0, 0, 0, static_cast<uint8_t>(t * 200)));
            break;
        }

        default:
            // 未知类型：直接 Fade
            {
                uint8_t alpha = static_cast<uint8_t>(t * 255);
                SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, alpha);
                SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);
                SDL_RenderFillRect(sdlRenderer, nullptr);
            }
            break;
    }
}

} // namespace sakura::scene
