#include "play_session.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sakura::game
{

void PlaySession::Initialize(ChartData& chart, bool autoPlay, int startMs, int endMs)
{
    m_chart = &chart;
    m_judge.Initialize();
    m_autoPlay = autoPlay;
    m_total = JudgmentCount(chart);
    m_time = startMs - 10000;
    m_holds.clear();
    m_sliders.clear();
    m_recording.clear();
    m_recording.reserve(30000);
    m_lanes.fill(false);
    m_mouseDown = false;
    m_keyboardCursor = m_mouseCursor = 0;
    for (auto& note : chart.keyboardNotes)
    {
        note.isJudged = note.time < startMs || (endMs > 0 && note.time + note.duration > endMs);
        note.result = note.isJudged ? JudgeResult::Miss : JudgeResult::None;
        note.alpha = note.isJudged ? 0.0f : 1.0f;
        if (note.isJudged) m_total -= note.type == NoteType::Hold ? 2 : 1;
    }
    for (auto& note : chart.mouseNotes)
    {
        note.isJudged = note.time < startMs || (endMs > 0 && note.time + note.sliderDuration > endMs);
        note.result = note.isJudged ? JudgeResult::Miss : JudgeResult::None;
        note.alpha = note.isJudged ? 0.0f : 1.0f;
        if (note.isJudged)
            m_total -= 1 + (note.type == NoteType::Slider ? static_cast<int>(note.sliderPath.size()) : 0);
    }
    m_score.Initialize(m_total);
}

void PlaySession::Emit(JudgmentEvent event)
{
    m_score.OnJudge(event.result, event.error, event.timed);
    if (m_onJudge) m_onJudge(event);
}

void PlaySession::Submit(SessionInput input)
{
    if (!m_chart) return;
    input.time = std::max(input.time, m_time);
    if (!m_recording.empty() && input.kind == InputKind::Tick && m_recording.back().kind == InputKind::Tick &&
        m_recording.back().time == input.time) return;
    if(input.kind == InputKind::Pointer && input.x == m_x && input.y == m_y) return;
    m_time = input.time;
    m_recording.push_back(input);
    switch (input.kind)
    {
    case InputKind::LaneDown:
        if (input.lane >= 0 && input.lane < 4 && !m_lanes[input.lane])
        {
            m_lanes[input.lane] = true;
            PressLane(input.lane, input.time);
        }
        break;
    case InputKind::LaneUp:
        if (input.lane >= 0 && input.lane < 4)
        {
            m_lanes[input.lane] = false;
            for (auto& hold : m_holds)
                if (m_chart->keyboardNotes[hold.noteIndex].lane == input.lane)
                {
                    hold.isHeld = false;
                    hold.releaseTimeMs = input.time;
                }
        }
        break;
    case InputKind::Pointer:
        m_x = input.x;
        m_y = input.y;
        break;
    case InputKind::PointerScale:
        SetPointerScale(input.x,input.y);
        break;
    case InputKind::MouseDown:
        m_x = input.x;
        m_y = input.y;
        if (!m_mouseDown) PressMouse(input.time);
        m_mouseDown = true;
        break;
    case InputKind::MouseUp:
        m_mouseDown = false;
        break;
    case InputKind::Resume:
        for (auto& hold : m_holds)
        {
            hold.lastHeldTimeMs = input.time + 120;
            hold.releaseTimeMs = -1;
        }
        for (auto& slider : m_sliders) slider.lastDownTimeMs = input.time + 120;
        break;
    case InputKind::Tick:
        Advance(input.time);
        break;
    }
}

void PlaySession::PressLane(int lane, int now)
{
    auto& notes = m_chart->keyboardNotes;
    for (size_t i = m_keyboardCursor; i < notes.size(); ++i)
    {
        auto& note = notes[i];
        if (note.time > now + m_judge.GetWindows().miss) break;
        if (note.isJudged || note.lane != lane) continue;
        // Expired heads cannot steal a fresh input from the next note.
        if (note.time < now - m_judge.GetWindows().miss) { MissKeyboard(note); continue; }
        const auto result = m_judge.JudgeKeyboardNote(note, now);
        if (result == JudgeResult::None) return;
        Emit({result, true, lane, 0, 0, note.time - now, true});
        note.isJudged = true;
        if (note.type == NoteType::Hold)
        {
            if (result == JudgeResult::Miss)
                Emit({JudgeResult::Miss, true, lane});
            else
            {
                note.result = JudgeResult::None;
                HoldState hold;
                hold.noteIndex = static_cast<int>(i);
                hold.isHeld = hold.headJudged = true;
                hold.headResult = result;
                hold.lastHeldTimeMs = now;
                m_holds.push_back(hold);
            }
        }
        return;
    }
}

void PlaySession::PressMouse(int now)
{
    auto& notes = m_chart->mouseNotes;
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t i = m_mouseCursor; i < notes.size(); ++i)
    {
        auto& note = notes[i];
        if (note.time > now + m_judge.GetWindows().bad) break;
        if (note.isJudged || std::abs(now - note.time) > m_judge.GetWindows().bad) continue;
        const float dx = (m_x - note.x)*m_scaleX, dy = (m_y - note.y)*m_scaleY;
        const float distance = dx * dx + dy * dy;
        const float radius = Judge::GetMouseHitTolerance(note);
        if (distance <= radius * radius && distance < bestDistance)
        {
            best = static_cast<int>(i);
            bestDistance = distance;
        }
    }
    if (best < 0) return;
    auto& note = notes[best];
    const auto result = m_judge.JudgeMouseNote(note, now, m_x, m_y);
    if (result == JudgeResult::None) return;
    note.isJudged = true;
    Emit({result, false, 0, note.x, note.y, note.time - now, true});
    if (note.type == NoteType::Slider)
    {
        note.result = JudgeResult::None;
        SliderState slider;
        slider.noteIndex = best;
        slider.headJudged = true;
        slider.headResult = result;
        slider.lastDownTimeMs = now;
        m_sliders.push_back(slider);
    }
}

void PlaySession::MissKeyboard(KeyboardNote& note)
{
    note.isJudged = true;
    note.result = JudgeResult::Miss;
    Emit({JudgeResult::Miss, true, note.lane});
    if (note.type == NoteType::Hold) Emit({JudgeResult::Miss, true, note.lane});
}

void PlaySession::MissMouse(MouseNote& note)
{
    note.isJudged = true;
    note.result = JudgeResult::Miss;
    Emit({JudgeResult::Miss, false, 0, note.x, note.y});
    if (note.type == NoteType::Slider)
        for (const auto& point : note.sliderPath)
            Emit({JudgeResult::Miss, false, 0, point.first, point.second});
}

void PlaySession::Advance(int now)
{
    const int cutoff = now - m_judge.GetWindows().miss;
    auto& keyboard = m_chart->keyboardNotes;
    auto& mouse = m_chart->mouseNotes;
    if (m_autoPlay)
    {
        for (size_t i = m_keyboardCursor; i < keyboard.size() && keyboard[i].time <= now; ++i)
            if (!keyboard[i].isJudged) PressLane(keyboard[i].lane, keyboard[i].time);
        for (size_t i = m_mouseCursor; i < mouse.size() && mouse[i].time <= now; ++i)
            if (!mouse[i].isJudged)
            {
                m_x = mouse[i].x;
                m_y = mouse[i].y;
                PressMouse(mouse[i].time);
            }
    }
    while (m_keyboardCursor < keyboard.size() && keyboard[m_keyboardCursor].time < cutoff)
    {
        auto& note = keyboard[m_keyboardCursor++];
        if (!note.isJudged) MissKeyboard(note);
    }
    while (m_mouseCursor < mouse.size() && mouse[m_mouseCursor].time < cutoff)
    {
        auto& note = mouse[m_mouseCursor++];
        if (!note.isJudged) MissMouse(note);
    }
    for (auto& hold : m_holds)
    {
        auto& note = keyboard[hold.noteIndex];
        if (m_autoPlay || m_lanes[note.lane])
        {
            hold.isHeld = true;
            hold.lastHeldTimeMs = now;
            hold.releaseTimeMs = -1;
        }
        else if (now - hold.lastHeldTimeMs <= HoldState::INPUT_GAP_TOLERANCE_MS)
            hold.isHeld = true;
        else
        {
            hold.isHeld = false;
            if (hold.releaseTimeMs < 0) hold.releaseTimeMs = now;
        }
        const auto result = m_judge.UpdateHoldTick(hold, note, now);
        if (result != JudgeResult::None)
        {
            // The tail rewards holding; timing is measured only on the head.
            note.result = result == JudgeResult::Miss ? result : JudgeResult::Perfect;
            Emit({note.result, true, note.lane});
        }
    }
    std::erase_if(m_holds, [](const HoldState& hold) { return hold.finalized; });
    for (auto& slider : m_sliders)
    {
        auto& note = mouse[slider.noteIndex];
        if (m_mouseDown) slider.lastDownTimeMs = now;
        const bool down = m_autoPlay || m_mouseDown ||
            (slider.lastDownTimeMs >= 0 && now - slider.lastDownTimeMs <= SliderState::INPUT_GAP_TOLERANCE_MS);
        while (!slider.finalized)
        {
            float x = m_x, y = m_y;
            if (m_autoPlay && slider.nextWaypointIndex < static_cast<int>(note.sliderPath.size()))
            {
                x = note.sliderPath[slider.nextWaypointIndex].first;
                y = note.sliderPath[slider.nextWaypointIndex].second;
                m_x = x; m_y = y;
            }
            const auto result = m_judge.UpdateSliderTracking(slider, note, now, x, y, down);
            if (result == JudgeResult::None) break;
            const auto& point = note.sliderPath[slider.nextWaypointIndex - 1];
            Emit({result, false, 0, point.first, point.second});
        }
        if (slider.finalized) note.result = slider.isMissed ? JudgeResult::Miss : slider.headResult;
    }
    std::erase_if(m_sliders, [](const SliderState& slider) { return slider.finalized; });
}

void PlaySession::Finish()
{
    if (!m_chart) return;
    for (auto& note : m_chart->keyboardNotes) if (!note.isJudged) MissKeyboard(note);
    for (auto& note : m_chart->mouseNotes) if (!note.isJudged) MissMouse(note);
    for (const auto& hold : m_holds)
    {
        auto& note = m_chart->keyboardNotes[hold.noteIndex];
        note.result = JudgeResult::Miss;
        Emit({JudgeResult::Miss, true, note.lane});
    }
    for (const auto& slider : m_sliders)
    {
        auto& note = m_chart->mouseNotes[slider.noteIndex];
        note.result = JudgeResult::Miss;
        for (size_t i = slider.nextWaypointIndex; i < note.sliderPath.size(); ++i)
            Emit({JudgeResult::Miss, false, 0, note.sliderPath[i].first, note.sliderPath[i].second});
    }
    m_holds.clear();
    m_sliders.clear();
}

} // namespace sakura::game
