#pragma once

// record_session.h — 离线逐帧录制模式（Sakura.exe --record <script.json>）
// 全部游戏逻辑与音乐时钟按固定虚拟时间（1/fps）推进；每帧渲染到任意分辨率的离屏纹理，
// 读回后交给 ffmpeg 编码。窗口只显示缩小预览，真实输入与焦点变化不会影响录制结果。

#include "core/app.h"
#include "core/record_script.h"
#include "core/video_encoder.h"
#include "game/chart.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sakura::scene
{

class Scene;

class RecordApp final : public sakura::core::App
{
public:
    RecordApp(sakura::core::RecordScript script,
              std::filesystem::path scriptPath,
              std::filesystem::path outputDirectory);
    ~RecordApp();

    // 初始化全部子系统、创建离屏录制目标并安装输入过滤；失败返回 false
    bool Start();

    // 收尾未完成的镜头、写出 manifest 并释放资源（可重复调用）
    void Stop(const std::string& failureReason = {});

    // 全部镜头成功返回 0，否则返回 1
    int GetExitCode() const;

protected:
    void OnUpdate(float dt) override;
    void OnEvent(const SDL_Event& event) override;
    void OnFrameBegin() override;
    void OnFrameEnd() override;

private:
    enum class Phase
    {
        Idle,       // 准备开始下一个镜头
        Entering,   // 已请求切换场景，等待 OnEnter
        Running,    // 镜头进行中（预热或录制）
        Finished    // 全部完成或已中止
    };

    const sakura::core::RecordShot& CurrentShot() const { return m_script.shots[m_shotIndex]; }
    long long VideoFrame() const { return m_shotFrame - m_warmupFrames; }

    bool ValidateKeys(std::string& error) const;
    void StartNextShot();
    std::unique_ptr<Scene> CreateShotScene(const sakura::core::RecordShot& shot, std::string& error);
    bool FindChart(const std::string& id, sakura::game::ChartInfo& chart, std::string& error) const;
    bool OpenShotOutputs(const sakura::core::RecordShot& shot, std::string& error);
    void EnterShot();
    void UpdateRunningShot();
    void CaptureFrame(SDL_Renderer* renderer);
    void FinishShot(bool success, const std::string& error);
    void RecordSkippedShot(const sakura::core::RecordShot& shot, const std::string& error);
    void FinishAll();
    void Abort(const std::string& reason);

    void DispatchEvents(double now);
    void InjectEvent(const sakura::core::RecordInputEvent& input);
    void InjectKeyTap(const char* keyName);
    bool IsResultVisible();

    void WriteCsvRow(long long frame);
    void WriteManifest();
    nlohmann::json ShotBaseEntry(const sakura::core::RecordShot& shot) const;
    void DrawPreview(SDL_Renderer* renderer, bool recorded);

    sakura::core::RecordScript m_script;
    std::filesystem::path      m_scriptPath;
    std::filesystem::path      m_outputDirectory;
    SDL_Texture*               m_target          = nullptr;
    bool                       m_filterInstalled = false;
    bool                       m_targetFailed    = false;
    bool                       m_stopped         = false;
    std::string                m_fatalError;

    // ── 当前镜头状态 ──────────────────────────────────────────────────────────
    Phase      m_phase           = Phase::Idle;
    size_t     m_shotIndex       = 0;
    Scene*     m_shotScene       = nullptr;
    int        m_enterFrames     = 0;
    long long  m_shotFrame       = 0;    // 自场景 OnEnter 起的帧序号（含预热）
    long long  m_warmupFrames    = 0;
    long long  m_endFrame        = -1;   // 录制终点（视频帧，不含）；until=result 时在结算出现后确定
    long long  m_tailFrames      = 0;
    long long  m_maxFrames       = 0;
    long long  m_resultFrame     = -1;
    long long  m_recordedFrames  = 0;
    bool       m_recordThisFrame = false;
    Uint64     m_shotStartTicks  = 0;
    Uint64     m_lastProgressLog = 0;
    Uint64     m_readbackNs      = 0;    // SDL_RenderReadPixels 累计耗时（含等待 GPU 完成本帧）
    Uint64     m_submitNs        = 0;    // 交给编码线程的累计耗时（含等待 ffmpeg 消化队列）

    std::vector<sakura::core::RecordInputEvent> m_events;
    size_t               m_eventCursor  = 0;
    float                m_mousePixelX  = 0.0f;
    float                m_mousePixelY  = 0.0f;
    SDL_MouseButtonFlags m_mouseButtons = 0;

    sakura::core::VideoEncoder m_encoder;
    std::ofstream              m_csv;
    nlohmann::json             m_shotEntry;
    long long                  m_musicStartFrame    = -1;
    double                     m_musicStartPosition = 0.0;
    float                      m_musicRate          = 1.0f;
    std::string                m_musicFile;

    // ── 汇总 ──────────────────────────────────────────────────────────────────
    nlohmann::json m_shots         = nlohmann::json::array();
    bool           m_allSucceeded  = true;
    bool           m_aborted       = false;
    std::string    m_abortReason;
    Uint64         m_runStartTicks = 0;
};

// --record 入口：准备隔离的用户目录与配置，依次录制脚本中的全部镜头并返回进程退出码
// （0 = 全部成功，1 = 存在失败或中止，2 = 脚本 / 输出目录无效）
int RunRecordMode(const std::filesystem::path& scriptPath, const std::filesystem::path& outputDirectory);

} // namespace sakura::scene
