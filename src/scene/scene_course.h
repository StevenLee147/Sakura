#pragma once

// scene_course.h — 樱华道场 · 段位认定场景 (v1.4.0)

#include "scene.h"
#include "scene_manager.h"
#include "core/renderer.h"
#include "core/resource_manager.h"
#include "effects/particle_system.h"
#include "game/chart.h"
#include "game/course.h"
#include "ui/button.h"
#include "ui/top_bar.h"

#include <memory>
#include <optional>
#include <vector>

namespace sakura::scene
{

class SceneCourse final : public Scene
{
public:
    explicit SceneCourse(SceneManager& mgr,
                         std::optional<sakura::game::CourseRecord> finishedSummary = std::nullopt);

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

    int SelectedIndex() const { return m_selected; }

private:
    SceneManager& m_manager;
    std::optional<sakura::game::CourseRecord> m_finishedSummary;
    bool m_showSummaryModal = false;

    sakura::core::FontHandle m_font = sakura::core::INVALID_HANDLE;
    std::unique_ptr<sakura::ui::TopBar> m_topBar;
    sakura::effects::ParticleSystem m_particles;
    int   m_petalEmitter = -1;
    float m_time         = 0.0f;

    std::vector<sakura::game::ChartInfo>                   m_allCharts;
    std::vector<std::optional<sakura::game::CourseRecord>> m_records;
    int m_selected = 0;
    int m_scroll   = 0;

    std::unique_ptr<sakura::ui::Button> m_btnBack;
    std::unique_ptr<sakura::ui::Button> m_btnStart;
    std::unique_ptr<sakura::ui::Button> m_btnCloseSummary;

    void RefreshRecords();
    void StartSelectedCourse();
};

} // namespace sakura::scene
