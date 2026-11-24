#pragma once

// scene_market.h — 在线谱面市场场景（搜索、排序、难度过滤、星级评价、SHA-256 校验安装）

#include "scene.h"
#include "scene_manager.h"
#include "effects/particle_system.h"
#include "net/api_client.h"
#include "ui/button.h"
#include "ui/progress_bar.h"
#include "ui/slider.h"
#include "ui/text_input.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sakura::scene
{

class SceneMarket final : public Scene
{
public:
    explicit SceneMarket(SceneManager& manager);

    void OnEnter() override;
    void OnExit() override;
    void OnUpdate(float dt) override;
    void OnRender(sakura::core::Renderer& renderer) override;
    void OnEvent(const SDL_Event& event) override;

private:
    struct UploadDialogState
    {
        std::mutex  mutex;
        bool        ready = false;
        std::string path;
    };

    SceneManager&                        m_manager;
    std::shared_ptr<bool>                m_alive  = std::make_shared<bool>(true);
    std::shared_ptr<UploadDialogState>   m_dialog = std::make_shared<UploadDialogState>();
    sakura::core::FontHandle             m_font   = sakura::core::INVALID_HANDLE;
    sakura::effects::ParticleSystem      m_particles;
    int                                  m_petalEmitter = -1;
    float                                m_time         = 0.0f;

    // 搜索、排序与难度过滤控件
    std::unique_ptr<sakura::ui::TextInput>   m_searchInput;
    std::array<std::unique_ptr<sakura::ui::Button>, 3> m_sortButtons;
    std::unique_ptr<sakura::ui::Slider>      m_minLevelSlider;
    std::unique_ptr<sakura::ui::Slider>      m_maxLevelSlider;
    std::unique_ptr<sakura::ui::Button>      m_btnApplyFilter;

    // 顶部与底部导航按钮
    std::unique_ptr<sakura::ui::Button>      m_btnUploadPackage;
    std::unique_ptr<sakura::ui::Button>      m_btnRefresh;
    std::unique_ptr<sakura::ui::Button>      m_btnGoLibrary;
    std::unique_ptr<sakura::ui::Button>      m_btnBack;

    // 右侧详情交互控件
    std::array<std::unique_ptr<sakura::ui::Button>, 5> m_starButtons;
    std::unique_ptr<sakura::ui::ProgressBar> m_downloadBar;
    std::unique_ptr<sakura::ui::Button>      m_btnDownload;
    std::unique_ptr<sakura::ui::Button>      m_btnPlayInstalled;

    // 市场数据状态
    std::string                              m_sortMode = "newest";
    std::vector<sakura::net::MarketChartItem> m_items;
    std::vector<sakura::net::MarketInstallState> m_installStates;
    std::vector<std::string>                 m_installedChartIds;
    int                                      m_selected = -1;
    int                                      m_scroll   = 0;
    int                                      m_total    = 0;
    bool                                     m_loading  = false;
    bool                                     m_downloading = false;
    bool                                     m_uploading   = false;
    std::string                              m_statusMessage;

    void RefreshMarket();
    void RefreshInstallStates();
    void SelectItem(int index);
    void UpdateSortButtonStyles();
    void UpdateDetailButtons();
    void StartDownloadSelected();
    void SubmitRating(int stars);
    void OpenUploadFileDialog();
    void UploadPackageFile(const std::string& filePath);
    void PlaySelectedInLibrary();
};

} // namespace sakura::scene
