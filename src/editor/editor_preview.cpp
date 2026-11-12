// editor_preview.cpp — 编辑器内试玩预览实现

#include "editor_preview.h"
#include "audio/audio_manager.h"
#include "core/theme.h"
#include "ui/visual_style.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <vector>

namespace sakura::editor
{

// ═════════════════════════════════════════════════════════════════════════════
// 构造
// ═════════════════════════════════════════════════════════════════════════════

EditorPreview::EditorPreview(EditorCore& core)
    : m_core(core)
{
}

// ═════════════════════════════════════════════════════════════════════════════
// Start / Stop
// ═════════════════════════════════════════════════════════════════════════════

void EditorPreview::Start(int fromMs)
{
    // 拷贝谱面音符
    m_kbNotes.clear();
    const auto& src = m_core.GetChartData().keyboardNotes;
    m_kbNotes.reserve(src.size());
    for (const auto& n : src)
    {
        PreviewKbNote pn;
        pn.note   = n;
        pn.note.isJudged = false;
        pn.note.result   = sakura::game::JudgeResult::None;
        pn.hit    = false;
        pn.missed = false;
        pn.hitFlash = 0.0f;
        m_kbNotes.push_back(std::move(pn));
    }

    m_startMs   = fromMs;
    m_currentMs = fromMs;
    m_active    = true;

    // 播放音频并跳转到指定位置
    const auto& info = m_core.GetChartInfo();
    if (!info.musicFile.empty() && !info.folderPath.empty())
    {
        std::string path = info.folderPath + "/" + info.musicFile;
        auto& am = sakura::audio::AudioManager::GetInstance();
        am.PlayMusic(path, 0);
        am.SetMusicPosition(static_cast<double>(fromMs) / 1000.0);
        LOG_INFO("[EditorPreview] 开始试玩，起始时间={}ms, 音乐={}", fromMs, path);
    }
    else
    {
        LOG_INFO("[EditorPreview] 开始试玩（无音乐），起始时间={}ms", fromMs);
    }
}

void EditorPreview::Stop()
{
    if (!m_active) return;
    m_active = false;
    sakura::audio::AudioManager::GetInstance().StopMusic();
    LOG_INFO("[EditorPreview] 退出试玩");
}

// ═════════════════════════════════════════════════════════════════════════════
// Update
// ═════════════════════════════════════════════════════════════════════════════

void EditorPreview::Update(float dt)
{
    if (!m_active) return;

    // 从音频同步时间（可能更精确）
    auto& am = sakura::audio::AudioManager::GetInstance();
    double pos = am.GetMusicPosition();
    if (pos > 0.001)
    {
        m_currentMs = static_cast<int>(pos * 1000.0);
    }
    else
    {
        // 无音乐时用计时器推进
        m_currentMs += static_cast<int>(dt * 1000.0f);
    }

    // 自动命中/错过 键盘音符
    for (auto& pn : m_kbNotes)
    {
        if (pn.hit || pn.missed) continue;

        int diff = pn.note.time - m_currentMs;

        // 自动命中
        if (std::abs(diff) <= static_cast<int>(AUTO_HIT_MS))
        {
            pn.hit      = true;
            pn.hitFlash = 0.25f;   // 0.25s 高亮
        }
        // 错过（超过 500ms 后）
        else if (diff < -500)
        {
            pn.missed = true;
        }

        // 衰减命中高亮
        if (pn.hitFlash > 0.0f)
            pn.hitFlash -= dt;
    }

    // 检查是否所有音符都结束且时间超过最后音符 +3s
    if (!m_kbNotes.empty())
    {
        int lastTime = 0;
        for (const auto& pn : m_kbNotes)
            lastTime = std::max(lastTime, pn.note.time + std::max(0, pn.note.duration));

        if (m_currentMs > lastTime + 3000)
        {
            Stop();
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// HandleEvent
// ═════════════════════════════════════════════════════════════════════════════

bool EditorPreview::HandleEvent(const SDL_Event& event)
{
    if (!m_active) return false;

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        if (event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            Stop();
            return true;
        }
    }
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// 坐标换算
// ═════════════════════════════════════════════════════════════════════════════

float EditorPreview::LaneToX(int lane) const
{
    return TRACK_X + lane * LANE_W;
}

float EditorPreview::TimeToY(int timeMs) const
{
    // timeMs == m_currentMs → 判定线 JUDGE_Y
    // 未来音符（timeMs > current）→ 屏幕上方（y 更小）
    float offset = static_cast<float>(timeMs - m_currentMs);
    float ratio  = offset / LEAD_TIME_MS;   // 1.0 = LEAD_TIME_MS 毫秒提前，在屏幕顶端
    return JUDGE_Y - ratio * (JUDGE_Y - (AREA_Y + 0.02f));
}

// ═════════════════════════════════════════════════════════════════════════════
// Render
// ═════════════════════════════════════════════════════════════════════════════

void EditorPreview::Render(sakura::core::Renderer& renderer)
{
    if (!m_active) return;

    DrawBackground(renderer);
    DrawLaneLines(renderer);
    DrawNotes(renderer);
    DrawMouseNotes(renderer);
    DrawJudgeLine(renderer);
    DrawHUD(renderer);
    DrawOverlay(renderer);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawBackground
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawBackground(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawGlassCard(renderer, { AREA_X, AREA_Y, AREA_W, AREA_H - 0.008f },
        { 12, 16, 32, 236 }, { 8, 10, 22, 248 },
        palette.gildedHairline, palette.surfaceBorder, 0.010f, 0.42f);

    for (int i = 0; i < 4; ++i)
    {
        const float x = LaneToX(i);
        const sakura::core::Color laneColor = (i % 2 == 0)
            ? sakura::core::Color{ 22, 26, 48, 175 }
            : sakura::core::Color{ 16, 20, 38, 155 };
        renderer.DrawFilledRect({ x, AREA_Y + 0.006f, LANE_W, AREA_H - 0.018f }, laneColor);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawLaneLines
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawLaneLines(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    for (int i = 0; i <= 4; ++i)
    {
        const float x = LaneToX(i);
        renderer.DrawLine(x, AREA_Y + 0.006f, x, AREA_Y + AREA_H - 0.012f,
            (i == 0 || i == 4) ? palette.gildedHairline : sakura::core::Color{ 140, 135, 185, 72 },
            (i == 0 || i == 4) ? 0.0014f : 0.0010f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawJudgeLine
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawJudgeLine(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    const float x0 = LaneToX(0);
    const float x1 = LaneToX(4);

    renderer.SetBlendMode(sakura::core::BlendMode::Additive);
    renderer.DrawRadialGradient((x0 + x1) * 0.5f, JUDGE_Y, (x1 - x0) * 0.55f, 0.016f,
        { palette.primary.r, palette.primary.g, palette.primary.b, 85 },
        { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
    renderer.SetBlendMode(sakura::core::BlendMode::Alpha);

    renderer.DrawLine(x0, JUDGE_Y, x1, JUDGE_Y, palette.accent, 0.0032f);

    const char* keyLabels[4] = { "A", "S", "D", "F" };
    for (int i = 0; i < 4; ++i)
    {
        const float x = LaneToX(i);
        const sakura::core::NormRect capRect{ x + LANE_W * 0.14f, JUDGE_Y + 0.014f, LANE_W * 0.72f, 0.028f };
        renderer.DrawRoundedGradientRect(capRect, 0.006f,
            { 30, 34, 60, 210 }, { 30, 34, 60, 210 }, { 20, 22, 42, 225 }, { 20, 22, 42, 225 });
        renderer.DrawRoundedRectOutlineGradient(capRect, 0.006f,
            palette.laneColors[i], palette.surfaceBorder, 0.0012f);
        if (m_font != sakura::core::INVALID_HANDLE)
        {
            renderer.DrawText(m_font, keyLabels[i], x + LANE_W * 0.5f, JUDGE_Y + 0.019f, 0.0155f,
                palette.text, sakura::core::TextAlign::Center);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawNotes
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawNotes(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    constexpr float NOTE_H = 0.011f;

    for (const auto& pn : m_kbNotes)
    {
        if (pn.missed) continue;
        if (pn.hit && pn.hitFlash <= 0.0f) continue;

        const float y = TimeToY(pn.note.time);
        if (y < AREA_Y - NOTE_H && pn.note.type != sakura::game::NoteType::Hold) continue;
        if (y > AREA_Y + AREA_H + NOTE_H) continue;

        const float x = LaneToX(pn.note.lane);
        const sakura::core::Color laneCol = palette.laneColors[std::clamp(pn.note.lane, 0, 3)];

        if (pn.note.type == sakura::game::NoteType::Hold && pn.note.duration > 0 && !pn.hit)
        {
            const float yEnd = TimeToY(pn.note.time + pn.note.duration);
            if (yEnd < y)
            {
                renderer.DrawRoundedGradientRect(
                    { x + LANE_W * 0.18f, std::max(AREA_Y, yEnd), LANE_W * 0.64f, y - std::max(AREA_Y, yEnd) }, 0.005f,
                    { 170, 235, 255, 190 }, { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 190 },
                    { palette.primary.r, palette.primary.g, palette.primary.b, 155 },
                    { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 155 });
            }
        }

        if (y < AREA_Y - NOTE_H) continue;

        const sakura::core::NormRect gemRect{ x + LANE_W * 0.07f, y - NOTE_H, LANE_W * 0.86f, NOTE_H * 2.0f };
        if (pn.hit)
        {
            const float flash = std::clamp(pn.hitFlash / 0.25f, 0.0f, 1.0f);
            renderer.SetBlendMode(sakura::core::BlendMode::Additive);
            renderer.DrawRadialGradient(x + LANE_W * 0.5f, JUDGE_Y, LANE_W * 0.9f, 0.032f,
                { palette.accent.r, palette.accent.g, palette.accent.b, static_cast<uint8_t>(180 * flash) },
                { palette.primary.r, palette.primary.g, palette.primary.b, 0 });
            renderer.SetBlendMode(sakura::core::BlendMode::Alpha);
        }
        else
        {
            const sakura::core::Color baseCol = (pn.note.type == sakura::game::NoteType::Hold)
                ? palette.holdColor
                : laneCol;
            renderer.DrawRoundedGradientRect(gemRect, 0.006f,
                { 255, 240, 248, 245 }, baseCol, baseCol, palette.primary);
            renderer.DrawRoundedRectOutlineGradient(gemRect, 0.006f,
                { 255, 255, 255, 215 }, palette.accent, 0.0013f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawMouseNotes
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawMouseNotes(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    constexpr float MA_X = 0.42f, MA_Y = 0.06f, MA_W = 0.33f, MA_H = 0.60f;

    const auto& mouseNotes = m_core.GetChartData().mouseNotes;
    for (const auto& mn : mouseNotes)
    {
        const int endWindow = (mn.type == sakura::game::NoteType::Slider)
            ? -(std::max(500, mn.sliderDuration + 300))
            : -350;
        const int diff = mn.time - m_currentMs;
        if (diff > 2400 || diff < endWindow) continue;

        const float approach = (diff > 0)
            ? std::clamp(static_cast<float>(diff) / 2400.0f, 0.0f, 1.0f)
            : 0.0f;

        const float screenX = MA_X + mn.x * MA_W;
        const float screenY = MA_Y + mn.y * MA_H;
        const float baseR   = 0.020f;
        const float ringR   = baseR * (1.0f + approach * 1.8f);
        const uint8_t alpha = static_cast<uint8_t>(std::clamp((1.0f - approach) * 195.0f + 50.0f, 50.0f, 235.0f));

        if (mn.type == sakura::game::NoteType::Circle)
        {
            renderer.DrawRingGradient(screenX, screenY, ringR - 0.0018f, ringR + 0.0020f,
                { palette.accent.r, palette.accent.g, palette.accent.b, static_cast<uint8_t>(alpha * 0.75f) },
                { palette.primary.r, palette.primary.g, palette.primary.b, 0 }, 40);
            renderer.DrawCircleFilled(screenX, screenY, baseR,
                { palette.circleColor.r, palette.circleColor.g, palette.circleColor.b, alpha }, 32);
            renderer.DrawCircleFilled(screenX, screenY, baseR * 0.42f,
                { 255, 245, 250, alpha }, 20);
            renderer.DrawCircleOutline(screenX, screenY, baseR, { 255, 255, 255, alpha }, 0.0018f, 36);
        }
        else if (mn.type == sakura::game::NoteType::Slider && mn.sliderPath.size() >= 2)
        {
            std::vector<sakura::core::RibbonNode> ribbon;
            ribbon.reserve(mn.sliderPath.size());
            for (const auto& pt : mn.sliderPath)
            {
                ribbon.push_back({
                    MA_X + pt.first * MA_W, MA_Y + pt.second * MA_H, 0.0095f,
                    { 175, 235, 255, alpha },
                    { palette.holdColor.r, palette.holdColor.g, palette.holdColor.b, 40 }
                });
            }
            renderer.DrawRibbonStrip(ribbon.data(), static_cast<int>(ribbon.size()), true);
            renderer.DrawCircleFilled(screenX, screenY, baseR * 0.9f,
                { palette.primary.r, palette.primary.g, palette.primary.b, alpha }, 28);
            renderer.DrawCircleOutline(screenX, screenY, baseR * 0.9f, { 255, 255, 255, alpha }, 0.0016f, 28);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawHUD
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawHUD(sakura::core::Renderer& renderer)
{
    if (m_font == sakura::core::INVALID_HANDLE) return;
    const auto& palette = sakura::core::Theme::GetInstance().Colors();

    const int curMs = m_currentMs;
    const float sec = static_cast<float>(std::abs(curMs)) / 1000.0f;
    std::ostringstream ss;
    ss << (curMs < 0 ? "-" : "")
       << static_cast<int>(sec) << "."
       << std::setw(3) << std::setfill('0') << (std::abs(curMs) % 1000) << "s";

    int hitCnt = 0;
    const int totalCnt = static_cast<int>(m_kbNotes.size());
    for (const auto& pn : m_kbNotes) if (pn.hit) ++hitCnt;

    const std::string hudText = "命中 " + std::to_string(hitCnt) + " / " + std::to_string(totalCnt)
                              + "   ·   " + ss.str();
    sakura::ui::VisualStyle::DrawBadgePill(renderer,
        { AREA_X + AREA_W * 0.5f - 0.12f, AREA_Y + AREA_H - 0.052f, 0.24f, 0.034f },
        hudText, m_font, 0.016f,
        { 20, 24, 44, 215 }, palette.gildedHairline, palette.accent);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawOverlay — 顶部 "试玩中" 提示条
// ─────────────────────────────────────────────────────────────────────────────
void EditorPreview::DrawOverlay(sakura::core::Renderer& renderer)
{
    const auto& palette = sakura::core::Theme::GetInstance().Colors();
    sakura::ui::VisualStyle::DrawBadgePill(renderer,
        { AREA_X + AREA_W * 0.5f - 0.14f, AREA_Y + 0.012f, 0.28f, 0.032f },
        "▶ 内嵌试玩预览中  ·  按 ESC 退出",
        m_font, 0.0155f,
        { 68, 28, 52, 225 }, palette.primary, palette.text);
}

} // namespace sakura::editor
