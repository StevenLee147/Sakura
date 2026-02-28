// editor_preview.cpp — 编辑器内试玩预览实现

#include "editor_preview.h"
#include "audio/audio_manager.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

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
    // 外层背景
    renderer.DrawFilledRect({ AREA_X, AREA_Y, AREA_W, AREA_H },
        sakura::core::Color{ 5, 4, 15, 240 });

    // 轨道背景
    for (int i = 0; i < 4; ++i)
    {
        float x  = LaneToX(i);
        sakura::core::Color laneColor = (i % 2 == 0)
            ? sakura::core::Color{ 18, 15, 42, 180 }
            : sakura::core::Color{ 22, 18, 50, 180 };
        renderer.DrawFilledRect({ x, AREA_Y, LANE_W, AREA_H }, laneColor);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawLaneLines
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawLaneLines(sakura::core::Renderer& renderer)
{
    sakura::core::Color lineColor{ 60, 50, 100, 120 };
    for (int i = 0; i <= 4; ++i)
    {
        float x = LaneToX(i);
        renderer.DrawLine(x, AREA_Y, x, AREA_Y + AREA_H, lineColor, 0.001f);
    }
    // 左右边界
    renderer.DrawLine(AREA_X, AREA_Y, AREA_X, AREA_Y + AREA_H, lineColor, 0.001f);
    renderer.DrawLine(AREA_X + AREA_W, AREA_Y, AREA_X + AREA_W, AREA_Y + AREA_H, lineColor, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawJudgeLine
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawJudgeLine(sakura::core::Renderer& renderer)
{
    float x0 = LaneToX(0);
    float x1 = LaneToX(4);
    renderer.DrawLine(x0, JUDGE_Y, x1, JUDGE_Y,
        sakura::core::Color{ 255, 200, 80, 220 }, 0.003f);

    // 轨道按键提示矩形
    for (int i = 0; i < 4; ++i)
    {
        float x = LaneToX(i);
        renderer.DrawFilledRect({ x + 0.002f, JUDGE_Y - 0.015f, LANE_W - 0.004f, 0.03f },
            sakura::core::Color{ 80, 70, 140, 150 });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawNotes
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawNotes(sakura::core::Renderer& renderer)
{
    constexpr float NOTE_H = 0.010f;  // 音符高度（归一化）

    for (const auto& pn : m_kbNotes)
    {
        if (pn.missed) continue;  // 错过不绘制
        if (pn.hit && pn.hitFlash <= 0.0f) continue;  // 命中且高亮退完

        float y = TimeToY(pn.note.time);
        // 裁剪：只绘制屏幕内的音符（稍微放宽范围）
        if (y < AREA_Y - NOTE_H || y > AREA_Y + AREA_H + NOTE_H) continue;

        float x = LaneToX(pn.note.lane);

        // 颜色
        sakura::core::Color noteColor{ 80, 130, 255, 220 };
        if (pn.note.type == sakura::game::NoteType::Hold)
            noteColor = { 80, 220, 120, 220 };
        else if (pn.note.type == sakura::game::NoteType::Drag)
            noteColor = { 255, 170, 60, 220 };

        if (pn.hit)
        {
            // 命中高亮
            float alpha = std::clamp(pn.hitFlash / 0.25f, 0.0f, 1.0f);
            noteColor.a = static_cast<Uint8>(200 * alpha + 20);
            noteColor   = { 255, 255, 180, noteColor.a };
        }

        // Hold：先画长条
        if (pn.note.type == sakura::game::NoteType::Hold && pn.note.duration > 0)
        {
            float yEnd = TimeToY(pn.note.time + pn.note.duration);
            if (yEnd < y)
            {
                renderer.DrawFilledRect({ x + LANE_W * 0.15f, yEnd,
                                          LANE_W * 0.70f, y - yEnd },
                    sakura::core::Color{ 60, 180, 90, 130 });
            }
        }

        // 音符本体
        renderer.DrawFilledRect({ x + LANE_W * 0.05f, y - NOTE_H,
                                   LANE_W * 0.90f, NOTE_H * 2.0f },
            noteColor);

        // Drag 箭头
        if (pn.note.type == sakura::game::NoteType::Drag
            && pn.note.dragToLane >= 0 && pn.note.dragToLane != pn.note.lane)
        {
            float xSrc = x + LANE_W * 0.5f;
            float xDst = LaneToX(pn.note.dragToLane) + LANE_W * 0.5f;
            renderer.DrawLine(xSrc, y, xDst, y,
                sakura::core::Color{ 255, 200, 80, 200 }, 0.002f);
            float dir = (xDst > xSrc) ? 1.0f : -1.0f;
            renderer.DrawLine(xDst, y, xDst - dir * 0.01f, y - 0.007f,
                sakura::core::Color{ 255, 200, 80, 200 }, 0.002f);
            renderer.DrawLine(xDst, y, xDst - dir * 0.01f, y + 0.007f,
                sakura::core::Color{ 255, 200, 80, 200 }, 0.002f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawMouseNotes
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawMouseNotes(sakura::core::Renderer& renderer)
{
    // 鼠标音符在右侧空白区显示接近圈（简化：在 AREA 右侧 1/3 处映射位置）
    // 鼠标编辑区归一化坐标 → 整屏归一化坐标
    constexpr float MA_X = 0.42f, MA_Y = 0.06f, MA_W = 0.33f, MA_H = 0.60f;

    const auto& mouseNotes = m_core.GetChartData().mouseNotes;
    for (const auto& mn : mouseNotes)
    {
        int diff = mn.time - m_currentMs;
        if (diff > 3000 || diff < -500) continue;   // 只显示接近范围

        // 接近圈：从 3000ms 开始缩小到命中时
        float approach = (diff > 0)
            ? std::clamp(static_cast<float>(diff) / 3000.0f, 0.0f, 1.0f)
            : 0.0f;  // 已过时 approach=0

        // 将鼠标编辑区归一化坐标转换为屏幕坐标
        float screenX = MA_X + mn.x * MA_W;
        float screenY = MA_Y + mn.y * MA_H;

        // 接近圈半径（从大到小）
        float baseR = 0.022f;
        float r     = baseR * (1.0f + approach * 2.0f);

        // 透明度随接近增加
        Uint8 alpha = static_cast<Uint8>(std::clamp((1.0f - approach) * 200.0f + 40.0f, 40.0f, 220.0f));

        if (mn.type == sakura::game::NoteType::Circle)
        {
            // 外圈（接近圈）
            renderer.DrawRectOutline({ screenX - r, screenY - r, r * 2.0f, r * 2.0f },
                sakura::core::Color{ 255, 120, 200, static_cast<Uint8>(alpha / 2) }, 0.002f);
            // 内圈
            renderer.DrawFilledRect({ screenX - baseR * 0.5f, screenY - baseR * 0.5f,
                                       baseR, baseR },
                sakura::core::Color{ 255, 120, 200, alpha });
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawHUD
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawHUD(sakura::core::Renderer& renderer)
{
    if (m_font == sakura::core::INVALID_HANDLE) return;

    // 当前时间
    int curMs = m_currentMs;
    float sec = static_cast<float>(std::abs(curMs)) / 1000.0f;
    std::ostringstream ss;
    ss << (curMs < 0 ? "-" : "")
       << static_cast<int>(sec) << "."
       << std::setw(3) << std::setfill('0') << (std::abs(curMs) % 1000) << "s";
    renderer.DrawText(m_font, ss.str(),
        AREA_X + AREA_W * 0.5f, AREA_Y + AREA_H - 0.025f, 0.018f,
        sakura::core::Color{ 200, 200, 200, 180 },
        sakura::core::TextAlign::Center);

    // 已命中/总数
    int hitCnt  = 0;
    int totalCnt = static_cast<int>(m_kbNotes.size());
    for (const auto& pn : m_kbNotes) if (pn.hit) ++hitCnt;

    std::string noteInfo = std::to_string(hitCnt) + " / " + std::to_string(totalCnt);
    renderer.DrawText(m_font, noteInfo,
        AREA_X + AREA_W * 0.5f, AREA_Y + AREA_H - 0.048f, 0.018f,
        sakura::core::Color{ 180, 255, 180, 180 },
        sakura::core::TextAlign::Center);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawOverlay — 顶部 "试玩中" 提示条
// ─────────────────────────────────────────────────────────────────────────────

void EditorPreview::DrawOverlay(sakura::core::Renderer& renderer)
{
    // 顶部提示条
    renderer.DrawFilledRect({ AREA_X, AREA_Y, AREA_W, 0.028f },
        sakura::core::Color{ 200, 60, 80, 190 });

    if (m_font != sakura::core::INVALID_HANDLE)
    {
        renderer.DrawText(m_font, "▶ 试玩中 — 按 ESC 退出",
            AREA_X + AREA_W * 0.5f, AREA_Y + 0.014f, 0.016f,
            sakura::core::Color{ 255, 255, 255, 230 },
            sakura::core::TextAlign::Center);
    }
}

} // namespace sakura::editor
