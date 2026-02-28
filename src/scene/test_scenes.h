#pragma once

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/input.h"
#include "core/resource_manager.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <memory>

namespace sakura::scene
{

// 前向声明（解决 A/B 互相引用）
class TestSceneA;
class TestSceneB;

// ── TestSceneA ────────────────────────────────────────────────────────────────

class TestSceneA final : public Scene
{
public:
    explicit TestSceneA(SceneManager& mgr) : m_manager(mgr) {}

    void OnEnter() override { LOG_INFO("[TestSceneA] 进入场景 A"); }
    void OnExit()  override { LOG_INFO("[TestSceneA] 退出场景 A"); }

    // 实现放在文件末尾（TestSceneB 完整定义后）
    void OnUpdate(float dt) override;

    void OnRender(sakura::core::Renderer& renderer) override
    {
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
            sakura::core::Color(80, 30, 15, 255));

        renderer.DrawFilledRect({ 0.2f, 0.2f, 0.6f, 0.6f },
            sakura::core::Color(220, 180, 130, 200));

        renderer.DrawRectOutline({ 0.2f, 0.2f, 0.6f, 0.6f },
            sakura::core::Color::White, 0.003f);

        auto font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();
        if (font != sakura::core::INVALID_HANDLE)
        {
            renderer.DrawText(font, "Scene A",
                0.5f, 0.42f, 0.07f,
                sakura::core::Color::White,
                sakura::core::TextAlign::Center);

            renderer.DrawText(font,
                "Space: Fade  |  Right Arrow: SlideLeft  |  Esc: Quit",
                0.5f, 0.60f, 0.022f,
                sakura::core::Color(220, 220, 220, 200),
                sakura::core::TextAlign::Center);
        }
    }

    void OnEvent(const SDL_Event&) override {}

private:
    SceneManager& m_manager;
};

// ── TestSceneB ────────────────────────────────────────────────────────────────

class TestSceneB final : public Scene
{
public:
    explicit TestSceneB(SceneManager& mgr) : m_manager(mgr) {}

    void OnEnter() override { LOG_INFO("[TestSceneB] 进入场景 B"); }
    void OnExit()  override { LOG_INFO("[TestSceneB] 退出场景 B"); }

    void OnUpdate(float dt) override;

    void OnRender(sakura::core::Renderer& renderer) override
    {
        renderer.DrawFilledRect({ 0.0f, 0.0f, 1.0f, 1.0f },
            sakura::core::Color(15, 30, 80, 255));

        renderer.DrawFilledRect({ 0.25f, 0.25f, 0.5f, 0.5f },
            sakura::core::Color(50, 120, 220, 200));

        renderer.DrawRectOutline({ 0.25f, 0.25f, 0.5f, 0.5f },
            sakura::core::Color(120, 180, 255), 0.003f);

        auto font = sakura::core::ResourceManager::GetInstance().GetDefaultFontHandle();
        if (font != sakura::core::INVALID_HANDLE)
        {
            renderer.DrawText(font, "Scene B",
                0.5f, 0.42f, 0.07f,
                sakura::core::Color::White,
                sakura::core::TextAlign::Center);

            renderer.DrawText(font,
                "Space: Fade  |  Left Arrow: SlideRight  |  Esc: Quit",
                0.5f, 0.60f, 0.022f,
                sakura::core::Color(180, 200, 240, 200),
                sakura::core::TextAlign::Center);
        }
    }

    void OnEvent(const SDL_Event&) override {}

private:
    SceneManager& m_manager;
};

// ── 方法实现（两个类都完整定义后才写）────────────────────────────────────────

inline void TestSceneA::OnUpdate(float /*dt*/)
{
    if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_SPACE))
    {
        m_manager.SwitchScene(
            std::make_unique<TestSceneB>(m_manager),
            TransitionType::Fade, 0.5f
        );
    }
    if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_RIGHT))
    {
        m_manager.SwitchScene(
            std::make_unique<TestSceneB>(m_manager),
            TransitionType::SlideLeft, 0.4f
        );
    }
}

inline void TestSceneB::OnUpdate(float /*dt*/)
{
    if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_SPACE))
    {
        m_manager.SwitchScene(
            std::make_unique<TestSceneA>(m_manager),
            TransitionType::Fade, 0.5f
        );
    }
    if (sakura::core::Input::IsKeyPressed(SDL_SCANCODE_LEFT))
    {
        m_manager.SwitchScene(
            std::make_unique<TestSceneA>(m_manager),
            TransitionType::SlideRight, 0.4f
        );
    }
}

} // namespace sakura::scene
