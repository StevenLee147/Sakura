// chart_converter.cpp — Phira (.pez) / osu! (.osz) / StepMania (.sm) 双区智能谱面转换引擎 (v1.7.0)
// 纯 C++20 跨平台实现（零 SDL / 平台专有头文件依赖）

#include "game/chart_converter.h"
#include "game/chart_loader.h"
#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace sakura::game
{

namespace
{

constexpr int   kMaxChartTimeMs   = 3600000;
constexpr float kMouseMinX        = 0.16f;
constexpr float kMouseMaxX        = 0.84f;
constexpr float kMouseMinY        = 0.18f;
constexpr float kMouseMaxY        = 0.82f;
constexpr float kSliderMinBound   = 0.14f;
constexpr float kSliderMaxBound   = 0.86f;
constexpr int   kMinSameLaneGapMs = 75;
constexpr int   kLinkToleranceMs  = 8;

std::string TrimSv(std::string_view sv)
{
    std::size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start])) != 0)
        ++start;
    std::size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])) != 0)
        --end;
    return std::string(sv.substr(start, end - start));
}

std::string StripQuotes(std::string_view sv)
{
    std::string s = TrimSv(sv);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::string_view StripUtf8Bom(std::string_view sv)
{
    if (sv.size() >= 3 &&
        static_cast<unsigned char>(sv[0]) == 0xEFU &&
        static_cast<unsigned char>(sv[1]) == 0xBBU &&
        static_cast<unsigned char>(sv[2]) == 0xBFU)
    {
        return sv.substr(3);
    }
    return sv;
}

std::string ToLowerAscii(std::string_view sv)
{
    std::string out;
    out.reserve(sv.size());
    for (char c : sv)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string PathToUtf8(const fs::path& p)
{
    const auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string PathExtLower(const fs::path& p)
{
    return ToLowerAscii(PathToUtf8(p.extension()));
}

bool ParseDoubleSafe(std::string_view sv, double& outVal)
{
    const std::string s = TrimSv(sv);
    if (s.empty())
        return false;
    char* endPtr = nullptr;
    const double v = std::strtod(s.c_str(), &endPtr);
    if (endPtr == s.c_str() || !std::isfinite(v))
        return false;
    outVal = v;
    return true;
}

bool ParseIntSafe(std::string_view sv, int& outVal)
{
    const std::string s = TrimSv(sv);
    if (s.empty())
        return false;
    char* endPtr = nullptr;
    const long v = std::strtol(s.c_str(), &endPtr, 10);
    if (endPtr == s.c_str())
        return false;
    outVal = static_cast<int>(std::clamp<long>(v, -2000000000L, 2000000000L));
    return true;
}

double JsonNumberOr(const json& obj, const char* key, double fallback)
{
    if (!obj.is_object() || !obj.contains(key))
        return fallback;
    const auto& v = obj[key];
    if (v.is_number())
    {
        const double d = v.get<double>();
        return std::isfinite(d) ? d : fallback;
    }
    if (v.is_string())
    {
        double parsed = fallback;
        if (ParseDoubleSafe(v.get_ref<const std::string&>(), parsed))
            return parsed;
    }
    return fallback;
}

int JsonIntOr(const json& obj, const char* key, int fallback)
{
    if (!obj.is_object() || !obj.contains(key))
        return fallback;
    const auto& v = obj[key];
    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_number())
        return static_cast<int>(std::llround(v.get<double>()));
    if (v.is_boolean())
        return v.get<bool>() ? 1 : 0;
    if (v.is_string())
    {
        int parsed = fallback;
        if (ParseIntSafe(v.get_ref<const std::string&>(), parsed))
            return parsed;
    }
    return fallback;
}

std::string JsonStringOr(const json& obj, const char* key, std::string_view fallback = "")
{
    if (!obj.is_object() || !obj.contains(key))
        return std::string(fallback);
    const auto& v = obj[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    if (v.is_number())
        return std::to_string(v.get<double>());
    return std::string(fallback);
}

float ClampBpm(double bpm, float fallback = 120.0f)
{
    if (!std::isfinite(bpm) || bpm <= 0.5)
        return std::clamp(fallback, 10.0f, 960.0f);
    return static_cast<float>(std::clamp(bpm, 10.0, 960.0));
}

float ParseLevelFromText(std::string_view text, float fallbackLevel = 0.0f)
{
    // Extract the last valid numeric token (e.g. "IN Lv.14.5", "AT 15", "HD Lv.8+")
    std::optional<float> found;
    std::size_t i = 0;
    while (i < text.size())
    {
        if (std::isdigit(static_cast<unsigned char>(text[i])) != 0)
        {
            std::size_t start = i;
            bool seenDot = false;
            while (i < text.size())
            {
                const char ch = text[i];
                if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
                {
                    ++i;
                }
                else if (ch == '.' && !seenDot && (i + 1) < text.size() &&
                         std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)
                {
                    seenDot = true;
                    ++i;
                }
                else
                {
                    break;
                }
            }
            double val = 0.0;
            if (ParseDoubleSafe(text.substr(start, i - start), val) && val > 0.0)
            {
                if (i < text.size() && text[i] == '+')
                    val += 0.5;
                found = static_cast<float>(std::clamp(val, 1.0, 15.0));
            }
        }
        else
        {
            ++i;
        }
    }
    if (found.has_value())
        return *found;
    if (fallbackLevel > 0.0f && std::isfinite(fallbackLevel))
        return std::clamp(fallbackLevel, 1.0f, 15.0f);
    return 0.0f;
}

std::string ExtractDiffNameFromLevelText(std::string_view levelText, std::string_view stemFallback = "")
{
    const std::string trimmed = TrimSv(levelText);
    if (!trimmed.empty())
    {
        const std::string upper = [&]() {
            std::string u = trimmed;
            for (char& c : u)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return u;
        }();
        for (const char* tag : { "AT", "IN", "HD", "EZ", "SP", "LEGACY" })
        {
            const std::string t(tag);
            if (upper == t || upper.rfind(t + " ", 0) == 0 || upper.rfind(t + ".", 0) == 0 ||
                upper.rfind(t + "_", 0) == 0 || upper.rfind(t + "LV", 0) == 0)
            {
                return t;
            }
        }
        // Take prefix before "Lv" or first digit if clean
        std::size_t cut = upper.find("LV");
        if (cut == std::string::npos)
        {
            for (std::size_t k = 0; k < trimmed.size(); ++k)
            {
                if (std::isdigit(static_cast<unsigned char>(trimmed[k])) != 0)
                {
                    cut = k;
                    break;
                }
            }
        }
        if (cut != std::string::npos && cut > 0)
        {
            std::string prefix = TrimSv(std::string_view(trimmed).substr(0, cut));
            while (!prefix.empty() && (prefix.back() == '-' || prefix.back() == ':' || prefix.back() == '.'))
                prefix.pop_back();
            prefix = TrimSv(prefix);
            if (!prefix.empty())
                return prefix;
        }
    }

    const std::string stemLower = ToLowerAscii(stemFallback);
    if (stemLower.find("ez") != std::string::npos || stemLower.find("easy") != std::string::npos)
        return "Easy";
    if (stemLower.find("hd") != std::string::npos || stemLower.find("hard") != std::string::npos)
        return "Hard";
    if (stemLower.find("at") != std::string::npos || stemLower.find("another") != std::string::npos)
        return "AT";
    if (stemLower.find("in") != std::string::npos || stemLower.find("expert") != std::string::npos ||
        stemLower.find("insane") != std::string::npos)
        return "IN";
    if (!stemFallback.empty() && stemLower != "chart" && stemLower != "data")
        return std::string(stemFallback);
    return "IN";
}

float EstimateChartLevel(const ChartData& data, float hintLevel = 0.0f)
{
    if (hintLevel >= 1.0f && std::isfinite(hintLevel))
        return std::clamp(std::round(hintLevel * 2.0f) * 0.5f, 1.0f, 15.0f);

    const std::size_t totalNotes = data.keyboardNotes.size() + data.mouseNotes.size();
    if (totalNotes == 0)
        return 1.0f;

    int minTime = kMaxChartTimeMs;
    int maxTime = 0;
    for (const auto& k : data.keyboardNotes)
    {
        minTime = std::min(minTime, k.time);
        maxTime = std::max(maxTime, k.time + k.duration);
    }
    for (const auto& m : data.mouseNotes)
    {
        minTime = std::min(minTime, m.time);
        maxTime = std::max(maxTime, m.time + m.sliderDuration);
    }
    const double spanSec = std::max(8.0, static_cast<double>(maxTime - minTime) / 1000.0);
    const double nps     = static_cast<double>(totalNotes) / spanSec;
    const double rawLv   = 1.5 + nps * 1.35;
    return static_cast<float>(std::clamp(std::round(rawLv * 2.0) * 0.5, 1.0, 15.0));
}

void PopulateDifficultyCounts(DifficultyInfo& diff, const ChartData& data)
{
    diff.noteCount = static_cast<int>(data.keyboardNotes.size());
    diff.holdCount = static_cast<int>(std::count_if(
        data.keyboardNotes.begin(), data.keyboardNotes.end(),
        [](const KeyboardNote& n) { return n.type == NoteType::Hold; }));
    diff.mouseNoteCount = static_cast<int>(data.mouseNotes.size());
    diff.thumbCount = static_cast<int>(std::count_if(
        data.keyboardNotes.begin(), data.keyboardNotes.end(),
        [](const KeyboardNote& n) { return n.lane == kThumbLaneIndex; }));
    diff.synergyCount = static_cast<int>(std::count_if(
        data.keyboardNotes.begin(), data.keyboardNotes.end(),
        [](const KeyboardNote& n) { return n.linkId > 0; })) +
        static_cast<int>(std::count_if(
            data.mouseNotes.begin(), data.mouseNotes.end(),
            [](const MouseNote& n) { return n.linkId > 0 || n.requireLane >= 0; }));
}

struct RawCandidateNote
{
    int    timeMs     = 0;
    int    durationMs = 0;
    double rawX       = 0.0;
    float  normX      = 0.5f;
    float  mouseYHint = 0.5f;
    int    kind       = 1; // 1=Tap, 2=Hold, 3=Flick, 4=Drag
    int    lineIndex  = 0;
    bool   above      = true;
};

void AutoNormalizeCandidateX(std::vector<RawCandidateNote>& notes, double nominalSymmetricSpan = 0.0)
{
    if (notes.empty())
        return;

    double minX    = notes.front().rawX;
    double maxX    = notes.front().rawX;
    double maxAbsX = std::abs(notes.front().rawX);
    for (const auto& n : notes)
    {
        if (!std::isfinite(n.rawX))
            continue;
        minX    = std::min(minX, n.rawX);
        maxX    = std::max(maxX, n.rawX);
        maxAbsX = std::max(maxAbsX, std::abs(n.rawX));
    }

    const bool looksUnitZeroOne = (nominalSymmetricSpan <= 0.0) &&
                                  (minX >= -0.001 && maxX <= 1.001) &&
                                  (minX > 0.04 || maxX > 0.55) &&
                                  (minX >= 0.0);

    double symSpan = nominalSymmetricSpan;
    if (!looksUnitZeroOne && symSpan <= 0.0)
    {
        if (maxAbsX <= 1.25)
            symSpan = 1.0;
        else if (maxAbsX <= 9.6)
            symSpan = std::clamp(maxAbsX, 5.5, 8.8);
        else
            symSpan = std::max(675.0, maxAbsX);
    }
    else if (symSpan > 0.0)
    {
        symSpan = std::max(symSpan, maxAbsX);
    }

    for (auto& n : notes)
    {
        const double rx = std::isfinite(n.rawX) ? n.rawX : 0.0;
        double nx = 0.5;
        if (looksUnitZeroOne)
            nx = std::clamp(rx, 0.0, 1.0);
        else
            nx = std::clamp(0.5 + rx / (2.0 * std::max(1e-6, symSpan)), 0.0, 1.0);
        n.normX = static_cast<float>(nx);
    }
}

class DualZoneLaneRouter
{
public:
    // Places a Tap or Hold onto lanes 0..3 (or 4 ThumbBar on strong central accents),
    // enforcing >= 75ms same-lane gap. Returns false if all lanes are busy -> caller routes to Mouse.
    bool TryPlaceKeyboard(int timeMs,
                          int durationMs,
                          float normX,
                          bool allowThumbAccent,
                          std::vector<KeyboardNote>& outKb)
    {
        const int clampedTime = std::clamp(timeMs, 0, kMaxChartTimeMs);
        const bool isHold     = (durationMs >= 50) && (clampedTime < kMaxChartTimeMs - 60);
        const int safeDur     = isHold ? std::clamp(durationMs, 60, kMaxChartTimeMs - clampedTime) : 0;

        if (allowThumbAccent && normX >= 0.34f && normX <= 0.66f &&
            clampedTime >= m_laneBusyUntil[kThumbLaneIndex] + 380)
        {
            KeyboardNote kb;
            kb.time     = clampedTime;
            kb.lane     = kThumbLaneIndex;
            kb.type     = isHold ? NoteType::Hold : NoteType::Tap;
            kb.duration = safeDur;
            outKb.push_back(kb);
            m_laneBusyUntil[kThumbLaneIndex] = clampedTime + safeDur;
            return true;
        }

        const int prefLane = std::clamp(static_cast<int>(normX * 4.0f), 0, kStandardLaneCount - 1);
        const std::array<int, 4> order = [&]() -> std::array<int, 4> {
            switch (prefLane)
            {
                case 0:  return { 0, 1, 2, 3 };
                case 1:  return (normX < 0.375f) ? std::array<int, 4>{ 1, 0, 2, 3 } : std::array<int, 4>{ 1, 2, 0, 3 };
                case 2:  return (normX < 0.625f) ? std::array<int, 4>{ 2, 1, 3, 0 } : std::array<int, 4>{ 2, 3, 1, 0 };
                default: return { 3, 2, 1, 0 };
            }
        }();

        for (int lane : order)
        {
            if (clampedTime >= m_laneBusyUntil[lane] + kMinSameLaneGapMs)
            {
                KeyboardNote kb;
                kb.time     = clampedTime;
                kb.lane     = lane;
                kb.type     = isHold ? NoteType::Hold : NoteType::Tap;
                kb.duration = safeDur;
                outKb.push_back(kb);
                m_laneBusyUntil[lane] = clampedTime + safeDur;
                return true;
            }
        }

        // Fallback to thumb bar if central and thumb bar is open
        if (clampedTime >= m_laneBusyUntil[kThumbLaneIndex] + 240)
        {
            KeyboardNote kb;
            kb.time     = clampedTime;
            kb.lane     = kThumbLaneIndex;
            kb.type     = isHold ? NoteType::Hold : NoteType::Tap;
            kb.duration = safeDur;
            outKb.push_back(kb);
            m_laneBusyUntil[kThumbLaneIndex] = clampedTime + safeDur;
            return true;
        }

        return false;
    }

    bool TryPlaceSpecificLane(int timeMs, int durationMs, int lane, std::vector<KeyboardNote>& outKb)
    {
        if (lane < 0 || lane > kThumbLaneIndex)
            return false;
        const int clampedTime = std::clamp(timeMs, 0, kMaxChartTimeMs);
        const int minGap      = (lane == kThumbLaneIndex) ? 200 : kMinSameLaneGapMs;
        if (clampedTime < m_laneBusyUntil[lane] + minGap)
            return false;
        const bool isHold = (durationMs >= 50) && (clampedTime < kMaxChartTimeMs - 60);
        const int safeDur = isHold ? std::clamp(durationMs, 60, kMaxChartTimeMs - clampedTime) : 0;
        KeyboardNote kb;
        kb.time     = clampedTime;
        kb.lane     = lane;
        kb.type     = isHold ? NoteType::Hold : NoteType::Tap;
        kb.duration = safeDur;
        outKb.push_back(kb);
        m_laneBusyUntil[lane] = clampedTime + safeDur;
        return true;
    }

private:
    std::array<int, kTotalKeyboardLanes> m_laneBusyUntil = { -10000, -10000, -10000, -10000, -10000 };
};

MouseNote MakeRawMouseCircle(int timeMs, float normX, float normY)
{
    MouseNote mn;
    mn.time = std::clamp(timeMs, 0, kMaxChartTimeMs);
    mn.x    = std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * std::clamp(normX, 0.0f, 1.0f), kMouseMinX, kMouseMaxX);
    mn.y    = std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * std::clamp(normY, 0.0f, 1.0f), kMouseMinY, kMouseMaxY);
    mn.type = NoteType::Circle;
    return mn;
}

MouseNote MakeRawMouseSlider(int timeMs, int durationMs, float normX, float normY, float endNormX, float endNormY)
{
    MouseNote mn = MakeRawMouseCircle(timeMs, normX, normY);
    mn.type      = NoteType::Slider;
    const int safeDur = std::clamp(durationMs, 80, std::max(80, kMaxChartTimeMs - mn.time));
    mn.sliderDuration = safeDur;

    float ex = std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * std::clamp(endNormX, 0.0f, 1.0f), kMouseMinX, kMouseMaxX);
    float ey = std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * std::clamp(endNormY, 0.0f, 1.0f), kMouseMinY, kMouseMaxY);
    if (std::hypot(ex - mn.x, ey - mn.y) < 0.10f)
    {
        const float dir = (mn.x < 0.5f) ? 1.0f : -1.0f;
        ex = std::clamp(mn.x + dir * 0.16f, kMouseMinX, kMouseMaxX);
        ey = std::clamp(mn.y + ((mn.y < 0.5f) ? 0.10f : -0.10f), kMouseMinY, kMouseMaxY);
    }
    const float mx = std::clamp(0.5f * (mn.x + ex) - (ey - mn.y) * 0.22f, kMouseMinX, kMouseMaxX);
    const float my = std::clamp(0.5f * (mn.y + ey) + (ex - mn.x) * 0.22f, kMouseMinY, kMouseMaxY);
    mn.sliderPath.emplace_back(mx, my);
    mn.sliderPath.emplace_back(ex, ey);
    return mn;
}

// Deduplicates simultaneous raw mouse candidates (<= 12ms) and groups consecutive Drags (<= 160ms) into 2-waypoint Sliders.
void BuildMouseNotesFromPhiraCandidates(std::vector<RawCandidateNote>& mouseCands,
                                        std::vector<MouseNote>& outMouse)
{
    if (mouseCands.empty())
        return;

    std::stable_sort(mouseCands.begin(), mouseCands.end(),
                     [](const RawCandidateNote& a, const RawCandidateNote& b) {
                         if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                         return a.kind < b.kind;
                     });

    // Collapse simultaneous candidates within 10ms so single-cursor mouse stays clean
    std::vector<RawCandidateNote> dedup;
    dedup.reserve(mouseCands.size());
    for (const auto& c : mouseCands)
    {
        if (!dedup.empty() && std::abs(c.timeMs - dedup.back().timeMs) <= 10)
        {
            if (c.durationMs > dedup.back().durationMs)
                dedup.back() = c;
            continue;
        }
        dedup.push_back(c);
    }

    std::size_t idx = 0;
    while (idx < dedup.size())
    {
        const auto& start = dedup[idx];
        if (start.kind == 4) // Drag: check for rapid consecutive Drag chain <= 160ms apart
        {
            std::size_t endIdx = idx;
            while (endIdx + 1 < dedup.size() &&
                   dedup[endIdx + 1].kind == 4 &&
                   (dedup[endIdx + 1].timeMs - dedup[endIdx].timeMs) >= 15 &&
                   (dedup[endIdx + 1].timeMs - dedup[endIdx].timeMs) <= 160 &&
                   (dedup[endIdx + 1].timeMs - start.timeMs) <= 850)
            {
                ++endIdx;
            }

            if (endIdx > idx && (dedup[endIdx].timeMs - start.timeMs) >= 60)
            {
                const auto& mid  = dedup[(idx + endIdx) / 2];
                const auto& last = dedup[endIdx];
                MouseNote slider = MakeRawMouseCircle(start.timeMs, start.normX, start.mouseYHint);
                slider.type           = NoteType::Slider;
                slider.sliderDuration = std::clamp(last.timeMs - start.timeMs, 60,
                                                   std::max(60, kMaxChartTimeMs - slider.time));
                const float ex = std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * last.normX, kMouseMinX, kMouseMaxX);
                const float ey = std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * last.mouseYHint, kMouseMinY, kMouseMaxY);
                float mx = std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * mid.normX, kMouseMinX, kMouseMaxX);
                float my = std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * mid.mouseYHint, kMouseMinY, kMouseMaxY);
                if (std::hypot(ex - slider.x, ey - slider.y) < 0.08f)
                {
                    mx = std::clamp(slider.x + ((slider.x < 0.5f) ? 0.10f : -0.10f), kMouseMinX, kMouseMaxX);
                    my = std::clamp(slider.y + ((slider.y < 0.5f) ? 0.12f : -0.12f), kMouseMinY, kMouseMaxY);
                }
                slider.sliderPath.emplace_back(mx, my);
                slider.sliderPath.emplace_back(
                    std::hypot(ex - slider.x, ey - slider.y) < 0.08f
                        ? std::clamp(slider.x + ((slider.x < 0.5f) ? 0.18f : -0.18f), kMouseMinX, kMouseMaxX)
                        : ex,
                    ey);
                outMouse.push_back(std::move(slider));
                idx = endIdx + 1;
                continue;
            }
        }

        if (start.durationMs >= 100)
        {
            const float endNx = std::clamp(start.normX + ((start.normX < 0.5f) ? 0.18f : -0.18f), 0.0f, 1.0f);
            const float endNy = std::clamp(start.mouseYHint + ((start.mouseYHint < 0.5f) ? 0.16f : -0.16f), 0.0f, 1.0f);
            outMouse.push_back(MakeRawMouseSlider(start.timeMs, start.durationMs, start.normX, start.mouseYHint, endNx, endNy));
        }
        else
        {
            outMouse.push_back(MakeRawMouseCircle(start.timeMs, start.normX, start.mouseYHint));
        }
        ++idx;
    }
}

void EnsureDualZoneBalance(std::vector<KeyboardNote>& kbNotes, std::vector<MouseNote>& msNotes)
{
    if (kbNotes.size() < 2)
        return;
    if (!msNotes.empty() && (kbNotes.size() < 8 || msNotes.size() * 10 >= kbNotes.size()))
        return;

    std::stable_sort(kbNotes.begin(), kbNotes.end(),
                     [](const KeyboardNote& a, const KeyboardNote& b) { return a.time < b.time; });

    int lastAddedTime = -10000;
    for (std::size_t i = 0; i < kbNotes.size(); ++i)
    {
        if (msNotes.size() * 7 >= kbNotes.size())
            break;
        const auto& k = kbNotes[i];
        if (k.time - lastAddedTime < 420)
            continue;
        bool nearExisting = false;
        for (const auto& m : msNotes)
        {
            if (std::abs(m.time - k.time) <= 220)
            {
                nearExisting = true;
                break;
            }
        }
        if (nearExisting)
            continue;

        // Prefer chord notes or periodic melodic notes (and guarantee >=1 if msNotes is still empty)
        const bool inChord = (i + 1 < kbNotes.size() && std::abs(kbNotes[i + 1].time - k.time) <= 6) ||
                             (i > 0 && std::abs(kbNotes[i - 1].time - k.time) <= 6);
        const bool fallbackLast = msNotes.empty() && (i + 1 == kbNotes.size());
        if (!inChord && (i % 6 != 3) && !fallbackLast)
            continue;

        const float nx = (k.lane >= 0 && k.lane < kStandardLaneCount)
                             ? (static_cast<float>(k.lane) + 0.5f) / 4.0f
                             : 0.5f;
        const float ny = 0.5f + 0.28f * std::sin(static_cast<float>(k.time) * 0.0021f + static_cast<float>(i));
        if (k.type == NoteType::Hold && k.duration >= 160)
            msNotes.push_back(MakeRawMouseSlider(k.time, std::min(k.duration, 900), nx, ny, 1.0f - nx, 1.0f - ny));
        else
            msNotes.push_back(MakeRawMouseCircle(k.time, nx, ny));
        lastAddedTime = k.time;
    }
}

void BindCrossZoneLinks(std::vector<KeyboardNote>& kbNotes, std::vector<MouseNote>& msNotes)
{
    std::stable_sort(kbNotes.begin(), kbNotes.end(),
                     [](const KeyboardNote& a, const KeyboardNote& b) { return a.time < b.time; });
    std::stable_sort(msNotes.begin(), msNotes.end(),
                     [](const MouseNote& a, const MouseNote& b) { return a.time < b.time; });

    int nextLinkId = 1;
    for (const auto& k : kbNotes)
        nextLinkId = std::max(nextLinkId, k.linkId + 1);
    for (const auto& m : msNotes)
        nextLinkId = std::max(nextLinkId, m.linkId + 1);

    std::size_t kbCursor = 0;
    for (auto& m : msNotes)
    {
        if (m.linkId > 0)
            continue;
        while (kbCursor < kbNotes.size() && kbNotes[kbCursor].time < m.time - kLinkToleranceMs)
            ++kbCursor;

        int         bestDiff = kLinkToleranceMs + 1;
        std::size_t bestIdx  = kbNotes.size();
        for (std::size_t j = kbCursor; j < kbNotes.size(); ++j)
        {
            if (kbNotes[j].time > m.time + kLinkToleranceMs)
                break;
            if (kbNotes[j].linkId > 0)
                continue;
            const int diff = std::abs(kbNotes[j].time - m.time);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestIdx  = j;
            }
        }
        if (bestIdx < kbNotes.size())
        {
            kbNotes[bestIdx].linkId = nextLinkId;
            m.linkId                = nextLinkId;
            ++nextLinkId;
        }
    }
}

void GenerateStageEventsForChart(ChartData& data)
{
    if (!data.stageEvents.empty())
        return;

    std::vector<int> hitTimes;
    hitTimes.reserve(data.keyboardNotes.size() + data.mouseNotes.size());
    for (const auto& k : data.keyboardNotes)
        hitTimes.push_back(k.time);
    for (const auto& m : data.mouseNotes)
        hitTimes.push_back(m.time);
    if (hitTimes.empty())
        return;

    std::sort(hitTimes.begin(), hitTimes.end());
    const int firstMs = hitTimes.front();
    const int lastMs  = hitTimes.back();
    constexpr int kWindowMs = 4000;
    const int bucketCount = std::max(1, (lastMs - firstMs) / kWindowMs + 1);

    std::vector<int> counts(static_cast<std::size_t>(bucketCount), 0);
    std::vector<int> firstInBucket(static_cast<std::size_t>(bucketCount), -1);
    int maxBucketCount = 0;
    for (int t : hitTimes)
    {
        const std::size_t b = static_cast<std::size_t>(std::clamp((t - firstMs) / kWindowMs, 0, bucketCount - 1));
        if (firstInBucket[b] < 0)
            firstInBucket[b] = t;
        ++counts[b];
        maxBucketCount = std::max(maxBucketCount, counts[b]);
    }

    if (maxBucketCount <= 0)
        return;

    const int threshold = std::max(3, static_cast<int>(std::lround(maxBucketCount * 0.58)));
    int lastEventTime   = -10000;
    for (int b = 0; b < bucketCount && data.stageEvents.size() < 48; ++b)
    {
        const std::size_t sb = static_cast<std::size_t>(b);
        if (counts[sb] < threshold && !(b == 0 && hitTimes.size() >= 2))
            continue;
        const int evTime = std::clamp(firstInBucket[sb] >= 0 ? firstInBucket[sb] : (firstMs + b * kWindowMs), 0, kMaxChartTimeMs);
        if (evTime - lastEventTime < 3200)
            continue;

        const float rel = static_cast<float>(counts[sb]) / static_cast<float>(maxBucketCount);
        StageEvent ev;
        ev.time = evTime;
        if (counts[sb] >= static_cast<int>(std::lround(maxBucketCount * 0.82)) && (data.stageEvents.size() % 2 == 1))
        {
            ev.type      = "petal_burst";
            ev.duration  = 900;
            ev.intensity = std::clamp(0.45f + 0.45f * rel, 0.35f, 1.10f);
            ev.colorR    = 255;
            ev.colorG    = 192;
            ev.colorB    = 216;
        }
        else
        {
            ev.type      = "pulse";
            ev.duration  = 560;
            ev.intensity = std::clamp(0.30f + 0.40f * rel, 0.25f, 0.90f);
            ev.colorR    = 246;
            ev.colorG    = 178;
            ev.colorB    = 206;
        }
        data.stageEvents.push_back(std::move(ev));
        lastEventTime = evTime;
    }
}

bool FinalizeAndValidateChart(ChartData& data, float fallbackBpm)
{
    data.version = 4;

    if (data.timingPoints.empty())
    {
        TimingPoint tp;
        tp.time               = 0;
        tp.bpm                = ClampBpm(fallbackBpm, 120.0f);
        tp.timeSigNumerator   = 4;
        tp.timeSigDenominator = 4;
        data.timingPoints.push_back(tp);
    }
    for (auto& tp : data.timingPoints)
    {
        tp.time               = std::clamp(tp.time, 0, kMaxChartTimeMs);
        tp.bpm                = ClampBpm(tp.bpm, 120.0f);
        tp.timeSigNumerator   = std::max(1, tp.timeSigNumerator);
        tp.timeSigDenominator = std::max(1, tp.timeSigDenominator);
    }
    std::stable_sort(data.timingPoints.begin(), data.timingPoints.end(),
                     [](const TimingPoint& a, const TimingPoint& b) { return a.time < b.time; });
    {
        std::vector<TimingPoint> dedupTp;
        dedupTp.reserve(data.timingPoints.size());
        for (const auto& tp : data.timingPoints)
        {
            if (!dedupTp.empty() && dedupTp.back().time == tp.time)
                dedupTp.back() = tp;
            else
                dedupTp.push_back(tp);
        }
        if (!dedupTp.empty() && dedupTp.front().time > 0)
        {
            TimingPoint zeroTp = dedupTp.front();
            zeroTp.time        = 0;
            dedupTp.insert(dedupTp.begin(), zeroTp);
        }
        data.timingPoints = std::move(dedupTp);
    }

    for (auto& sv : data.svPoints)
    {
        sv.time  = std::clamp(sv.time, 0, kMaxChartTimeMs);
        sv.speed = std::isfinite(sv.speed) ? std::clamp(sv.speed, 0.15f, 6.0f) : 1.0f;
        if (sv.easing.empty())
            sv.easing = "linear";
    }
    std::stable_sort(data.svPoints.begin(), data.svPoints.end(),
                     [](const SVPoint& a, const SVPoint& b) { return a.time < b.time; });
    {
        std::vector<SVPoint> dedupSv;
        dedupSv.reserve(data.svPoints.size());
        for (const auto& sv : data.svPoints)
        {
            if (!dedupSv.empty() && dedupSv.back().time == sv.time)
                dedupSv.back() = sv;
            else
                dedupSv.push_back(sv);
        }
        data.svPoints = std::move(dedupSv);
    }

    for (auto& k : data.keyboardNotes)
    {
        k.time   = std::clamp(k.time, 0, kMaxChartTimeMs);
        k.lane   = std::clamp(k.lane, 0, kThumbLaneIndex);
        k.linkId = std::max(0, k.linkId);
        if (k.type == NoteType::Hold)
        {
            const int maxDur = kMaxChartTimeMs - k.time;
            if (k.duration <= 0 || maxDur <= 0)
            {
                k.type     = NoteType::Tap;
                k.duration = 0;
            }
            else
            {
                k.duration = std::clamp(k.duration, 1, maxDur);
            }
        }
        else
        {
            k.type     = NoteType::Tap;
            k.duration = 0;
        }
    }

    if (data.keyboardNotes.size() + data.mouseNotes.size() > 99000)
    {
        if (data.keyboardNotes.size() > 75000)
            data.keyboardNotes.resize(75000);
        if (data.mouseNotes.size() > 24000)
            data.mouseNotes.resize(24000);
    }

    std::stable_sort(data.keyboardNotes.begin(), data.keyboardNotes.end(),
                     [](const KeyboardNote& a, const KeyboardNote& b) { return a.time < b.time; });
    std::stable_sort(data.mouseNotes.begin(), data.mouseNotes.end(),
                     [](const MouseNote& a, const MouseNote& b) { return a.time < b.time; });
    std::stable_sort(data.stageEvents.begin(), data.stageEvents.end(),
                     [](const StageEvent& a, const StageEvent& b) { return a.time < b.time; });
    std::stable_sort(data.lyrics.begin(), data.lyrics.end(),
                     [](const LyricLine& a, const LyricLine& b) { return a.time < b.time; });

    ChartLoader loader;
    return loader.ValidateChartData(data);
}

json SerializeChartDataJson(const ChartData& data)
{
    json j;
    j["version"] = 4;

    json tpArr = json::array();
    for (const auto& tp : data.timingPoints)
    {
        tpArr.push_back({
            { "time", tp.time },
            { "bpm", tp.bpm },
            { "time_signature", { tp.timeSigNumerator, tp.timeSigDenominator } }
        });
    }
    j["timing_points"] = std::move(tpArr);

    json svArr = json::array();
    for (const auto& sv : data.svPoints)
    {
        svArr.push_back({
            { "time", sv.time },
            { "speed", sv.speed },
            { "easing", sv.easing.empty() ? "linear" : sv.easing }
        });
    }
    j["sv_points"] = std::move(svArr);

    json kbArr = json::array();
    for (const auto& n : data.keyboardNotes)
    {
        json item;
        item["time"]     = n.time;
        item["lane"]     = n.lane;
        item["type"]     = (n.type == NoteType::Hold) ? "hold" : "tap";
        item["duration"] = n.duration;
        if (n.linkId > 0)
            item["link_id"] = n.linkId;
        kbArr.push_back(std::move(item));
    }
    j["keyboard_notes"] = std::move(kbArr);

    json msArr = json::array();
    for (const auto& n : data.mouseNotes)
    {
        json item;
        item["time"]            = n.time;
        item["x"]               = n.x;
        item["y"]               = n.y;
        item["type"]            = (n.type == NoteType::Slider) ? "slider" : "circle";
        item["slider_duration"] = n.sliderDuration;
        if (n.linkId > 0)
            item["link_id"] = n.linkId;
        if (n.requireLane >= 0)
            item["require_lane"] = n.requireLane;
        if (!n.sliderPath.empty())
        {
            json pathArr = json::array();
            for (const auto& [px, py] : n.sliderPath)
                pathArr.push_back({ px, py });
            item["slider_path"] = std::move(pathArr);
        }
        msArr.push_back(std::move(item));
    }
    j["mouse_notes"] = std::move(msArr);

    if (!data.stageEvents.empty())
    {
        json evArr = json::array();
        for (const auto& ev : data.stageEvents)
        {
            evArr.push_back({
                { "time", ev.time },
                { "duration", ev.duration },
                { "type", ev.type },
                { "intensity", ev.intensity },
                { "color", { ev.colorR, ev.colorG, ev.colorB } }
            });
        }
        j["stage_events"] = std::move(evArr);
    }

    if (!data.lyrics.empty())
    {
        json lyArr = json::array();
        for (const auto& line : data.lyrics)
        {
            lyArr.push_back({
                { "time", line.time },
                { "end_time", line.endTime },
                { "text", line.text },
                { "sub_text", line.subText }
            });
        }
        j["lyrics"] = std::move(lyArr);
    }

    return j;
}

double ParseRpeBeatTriplet(const json& val)
{
    if (val.is_array() && val.size() >= 3)
    {
        const double bar = val[0].is_number() ? val[0].get<double>() : 0.0;
        const double num = val[1].is_number() ? val[1].get<double>() : 0.0;
        const double den = val[2].is_number() ? val[2].get<double>() : 1.0;
        return bar + ((std::abs(den) > 1e-9) ? (num / den) : 0.0);
    }
    if (val.is_number())
        return val.get<double>();
    return 0.0;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. OptimizeMouseSpatialLayout
// ═════════════════════════════════════════════════════════════════════════════

void ChartConverter::OptimizeMouseSpatialLayout(std::vector<MouseNote>& mouseNotes)
{
    if (mouseNotes.empty())
        return;

    for (auto& n : mouseNotes)
    {
        n.time   = std::clamp(n.time, 0, kMaxChartTimeMs);
        n.linkId = std::max(0, n.linkId);
        if (!std::isfinite(n.x)) n.x = 0.5f;
        if (!std::isfinite(n.y)) n.y = 0.5f;
    }

    std::stable_sort(mouseNotes.begin(), mouseNotes.end(),
                     [](const MouseNote& a, const MouseNote& b) { return a.time < b.time; });

    auto mapRawX = [](float x) -> float {
        if (x >= kMouseMinX && x <= kMouseMaxX)
            return x;
        return std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * std::clamp(x, 0.0f, 1.0f), kMouseMinX, kMouseMaxX);
    };
    auto mapRawY = [](float y) -> float {
        if (y >= kMouseMinY && y <= kMouseMaxY)
            return y;
        return std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * std::clamp(y, 0.0f, 1.0f), kMouseMinY, kMouseMaxY);
    };

    const std::size_t count = mouseNotes.size();
    std::vector<std::pair<float, float>> rawHead(count);
    std::vector<std::pair<float, float>> rawEnd(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const float hx = mapRawX(mouseNotes[i].x);
        const float hy = mapRawY(mouseNotes[i].y);
        rawHead[i]     = { hx, hy };
        if (mouseNotes[i].type == NoteType::Slider && !mouseNotes[i].sliderPath.empty())
        {
            const auto& back = mouseNotes[i].sliderPath.back();
            const float bx   = std::isfinite(back.first) ? std::clamp(back.first, kSliderMinBound, kSliderMaxBound) : hx;
            const float by   = std::isfinite(back.second) ? std::clamp(back.second, kSliderMinBound, kSliderMaxBound) : hy;
            rawEnd[i]        = { bx, by };
        }
        else
        {
            rawEnd[i] = { hx, hy };
        }
    }

    auto finalizeSliderGeometry = [&](MouseNote& note, float oldHx, float oldHy) {
        if (note.type != NoteType::Slider)
        {
            note.sliderDuration = 0;
            note.sliderPath.clear();
            return;
        }
        const int maxDur = kMaxChartTimeMs - note.time;
        if (maxDur <= 0)
        {
            note.type           = NoteType::Circle;
            note.sliderDuration = 0;
            note.sliderPath.clear();
            return;
        }
        if (note.sliderDuration <= 0)
            note.sliderDuration = std::min(180, maxDur);
        else
            note.sliderDuration = std::clamp(note.sliderDuration, 1, maxDur);

        const float dx = note.x - oldHx;
        const float dy = note.y - oldHy;
        for (auto& [wx, wy] : note.sliderPath)
        {
            if (!std::isfinite(wx)) wx = note.x;
            if (!std::isfinite(wy)) wy = note.y;
            wx = std::clamp(wx + dx, kSliderMinBound, kSliderMaxBound);
            wy = std::clamp(wy + dy, kSliderMinBound, kSliderMaxBound);
        }
        if (note.sliderPath.size() > 512)
            note.sliderPath.resize(512);
        if (note.sliderPath.empty())
        {
            const float span = std::clamp(static_cast<float>(note.sliderDuration) / 1100.0f, 0.10f, 0.24f);
            const float dirX = (note.x <= 0.50f) ? 1.0f : -1.0f;
            const float dirY = (note.y <= 0.50f) ? 0.65f : -0.65f;
            const float len  = std::hypot(dirX, dirY);
            const float ex   = std::clamp(note.x + (dirX / len) * span, kMouseMinX, kMouseMaxX);
            const float ey   = std::clamp(note.y + (dirY / len) * span, kMouseMinY, kMouseMaxY);
            note.sliderPath.emplace_back(0.5f * (note.x + ex), 0.5f * (note.y + ey));
            note.sliderPath.emplace_back(ex, ey);
        }
        else
        {
            // Keep slider tail inside comfortable bounds so subsequent jumps can originate cleanly
            auto& tail  = note.sliderPath.back();
            tail.first  = std::clamp(tail.first, kSliderMinBound, kSliderMaxBound);
            tail.second = std::clamp(tail.second, kSliderMinBound, kSliderMaxBound);
        }
    };

    // Trim any slider that would overrun the following note's head
    for (std::size_t i = 0; i + 1 < count; ++i)
    {
        if (mouseNotes[i].type == NoteType::Slider)
        {
            const int gap = mouseNotes[i + 1].time - mouseNotes[i].time - 15;
            if (gap < 50)
            {
                mouseNotes[i].type           = NoteType::Circle;
                mouseNotes[i].sliderDuration = 0;
                mouseNotes[i].sliderPath.clear();
            }
            else if (mouseNotes[i].sliderDuration > gap)
            {
                mouseNotes[i].sliderDuration = gap;
            }
        }
    }

    // Place first mouse note
    {
        const float oldX = mouseNotes[0].x;
        const float oldY = mouseNotes[0].y;
        mouseNotes[0].x  = rawHead[0].first;
        mouseNotes[0].y  = rawHead[0].second;
        finalizeSliderGeometry(mouseNotes[0], oldX, oldY);
    }

    constexpr float kTwoPi          = 2.0f * std::numbers::pi_v<float>;
    constexpr float kMinDist1000    = 0.1815f; // Strict margin above 0.18f
    constexpr float kMinDist400     = 0.1050f; // Strict margin above 0.10f
    constexpr std::array<float, 9> kRadiusMultipliers = {
        1.00f, 1.07f, 0.93f, 1.16f, 0.86f, 1.28f, 0.76f, 1.42f, 0.64f
    };

    for (std::size_t i = 1; i < count; ++i)
    {
        const auto& prev = mouseNotes[i - 1];
        auto&       curr = mouseNotes[i];

        const int prevEndTime = prev.time + (prev.type == NoteType::Slider ? std::max(0, prev.sliderDuration) : 0);
        float prevEndX        = prev.x;
        float prevEndY        = prev.y;
        if (prev.type == NoteType::Slider && !prev.sliderPath.empty())
        {
            prevEndX = prev.sliderPath.back().first;
            prevEndY = prev.sliderPath.back().second;
        }

        const float dtMs       = std::max(0.0f, static_cast<float>(curr.time - prevEndTime));
        const float targetJump = std::clamp(0.15f + 0.50f * (1.0f - std::exp(-dtMs / 620.0f)), 0.15f, 0.68f);
        const bool  prevIsRecentCircle = (prev.type == NoteType::Circle) && ((curr.time - prev.time) <= 1000);
        const float effTarget  = prevIsRecentCircle ? std::max(targetJump, kMinDist1000) : targetJump;

        // Preserve directional / height intent from source coordinates
        float intentDx = rawHead[i].first - rawEnd[i - 1].first;
        float intentDy = rawHead[i].second - rawEnd[i - 1].second;
        if (std::hypot(intentDx, intentDy) < 0.025f)
        {
            intentDx = rawHead[i].first - prevEndX;
            intentDy = rawHead[i].second - prevEndY;
        }
        float baseAngle = 0.0f;
        if (std::hypot(intentDx, intentDy) >= 0.025f)
        {
            baseAngle = std::atan2(intentDy, intentDx);
        }
        else
        {
            const float toCenterDx = 0.50f - prevEndX;
            const float toCenterDy = 0.50f - prevEndY;
            const float centerBias = (std::hypot(toCenterDx, toCenterDy) > 0.14f)
                                         ? std::atan2(toCenterDy, toCenterDx)
                                         : 0.0f;
            baseAngle = centerBias + static_cast<float>(i) * 2.39996323f;
        }

        float bestTier1Cost = std::numeric_limits<float>::infinity();
        float bestTier1X    = rawHead[i].first;
        float bestTier1Y    = rawHead[i].second;
        bool  foundTier1    = false;

        float bestTier2Score = -1.0f;
        float bestTier2X     = rawHead[i].first;
        float bestTier2Y     = rawHead[i].second;
        bool  foundTier2     = false;

        float bestTier3Dist = -1.0f;
        float bestTier3X    = rawHead[i].first;
        float bestTier3Y    = rawHead[i].second;

        auto evaluateCandidate = [&](float cx, float cy) {
            if (cx < kMouseMinX || cx > kMouseMaxX || cy < kMouseMinY || cy > kMouseMaxY)
                return;

            float minDist1000 = std::numeric_limits<float>::infinity();
            float minDist400  = std::numeric_limits<float>::infinity();
            for (std::size_t j = i; j > 0; --j)
            {
                const auto& earlier = mouseNotes[j - 1];
                const int   ageMs   = curr.time - earlier.time;
                if (ageMs > 1000)
                    break;
                const float d = std::hypot(cx - earlier.x, cy - earlier.y);
                minDist1000   = std::min(minDist1000, d);
                if (ageMs <= 400)
                    minDist400 = std::min(minDist400, d);
            }

            if (minDist400 > bestTier3Dist)
            {
                bestTier3Dist = minDist400;
                bestTier3X    = cx;
                bestTier3Y    = cy;
            }

            const float jumpDist = std::hypot(cx - prevEndX, cy - prevEndY);
            if (minDist1000 >= kMinDist1000 && minDist400 >= kMinDist400)
            {
                const float candAngle = std::atan2(cy - prevEndY, cx - prevEndX);
                float       angDiff   = std::abs(candAngle - baseAngle);
                while (angDiff > kTwoPi) angDiff -= kTwoPi;
                if (angDiff > std::numbers::pi_v<float>)
                    angDiff = kTwoPi - angDiff;

                const float rawAnchorDist = std::hypot(cx - rawHead[i].first, cy - rawHead[i].second);
                const float heightDiff    = std::abs(cy - rawHead[i].second);
                const float cost = 2.8f * std::abs(jumpDist - effTarget) +
                                   0.45f * angDiff +
                                   0.35f * rawAnchorDist +
                                   0.30f * heightDiff;
                if (cost < bestTier1Cost)
                {
                    bestTier1Cost = cost;
                    bestTier1X    = cx;
                    bestTier1Y    = cy;
                    foundTier1    = true;
                }
            }
            else if (minDist400 >= kMinDist400)
            {
                const float score = minDist1000 - 0.35f * std::abs(jumpDist - effTarget);
                if (score > bestTier2Score)
                {
                    bestTier2Score = score;
                    bestTier2X     = cx;
                    bestTier2Y     = cy;
                    foundTier2     = true;
                }
            }
        };

        // Polar ring sampling around prevEndPos near targetJump
        constexpr int kAngleSteps = 48;
        for (float mult : kRadiusMultipliers)
        {
            const float r = std::clamp(effTarget * mult, 0.15f, 0.85f);
            for (int a = 0; a < kAngleSteps; ++a)
            {
                const int   halfStep = (a + 1) / 2;
                const float sign     = (a % 2 == 0) ? 1.0f : -1.0f;
                const float theta    = baseAngle + sign * static_cast<float>(halfStep) * (kTwoPi / static_cast<float>(kAngleSteps));
                const float dx       = std::cos(theta);
                const float dy       = std::sin(theta);
                const float cx       = prevEndX + r * dx;
                const float cy       = prevEndY + r * dy;
                if (cx >= kMouseMinX && cx <= kMouseMaxX && cy >= kMouseMinY && cy <= kMouseMaxY)
                {
                    evaluateCandidate(cx, cy);
                }
                else
                {
                    float tMax = r;
                    if (dx > 1e-5f)       tMax = std::min(tMax, (kMouseMaxX - prevEndX) / dx);
                    else if (dx < -1e-5f) tMax = std::min(tMax, (kMouseMinX - prevEndX) / dx);
                    if (dy > 1e-5f)       tMax = std::min(tMax, (kMouseMaxY - prevEndY) / dy);
                    else if (dy < -1e-5f) tMax = std::min(tMax, (kMouseMinY - prevEndY) / dy);
                    if (tMax > 0.0f)
                    {
                        evaluateCandidate(
                            std::clamp(prevEndX + tMax * dx, kMouseMinX, kMouseMaxX),
                            std::clamp(prevEndY + tMax * dy, kMouseMinY, kMouseMaxY));
                    }
                }
            }
        }

        // Also evaluate clamped polar rays & a uniform 13x13 playfield grid so corners/long jumps always find solutions
        evaluateCandidate(rawHead[i].first, rawHead[i].second);
        for (int gx = 0; gx < 13; ++gx)
        {
            const float cx = kMouseMinX + (kMouseMaxX - kMouseMinX) * (static_cast<float>(gx) / 12.0f);
            for (int gy = 0; gy < 13; ++gy)
            {
                const float cy = kMouseMinY + (kMouseMaxY - kMouseMinY) * (static_cast<float>(gy) / 12.0f);
                evaluateCandidate(cx, cy);
            }
        }

        const float oldX = curr.x;
        const float oldY = curr.y;
        if (foundTier1)
        {
            curr.x = bestTier1X;
            curr.y = bestTier1Y;
        }
        else if (foundTier2)
        {
            curr.x = bestTier2X;
            curr.y = bestTier2Y;
        }
        else
        {
            curr.x = std::clamp(bestTier3X, kMouseMinX, kMouseMaxX);
            curr.y = std::clamp(bestTier3Y, kMouseMinY, kMouseMaxY);
        }

        finalizeSliderGeometry(curr, oldX, oldY);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. ConvertPhigrosOfficialJson
// ═════════════════════════════════════════════════════════════════════════════

std::optional<ChartData> ChartConverter::ConvertPhigrosOfficialJson(std::string_view jsonText,
                                                                    float fallbackBpm)
{
    json root;
    try
    {
        root = json::parse(StripUtf8Bom(jsonText));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[ChartConverter] Phigros JSON 解析失败: {}", e.what());
        return std::nullopt;
    }

    if (!root.is_object() || !root.contains("judgeLineList") || !root["judgeLineList"].is_array())
        return std::nullopt;

    const int    formatVersion = JsonIntOr(root, "formatVersion", 3);
    const double rawOffset     = JsonNumberOr(root, "offset", 0.0);
    // Phigros Official offset is in seconds (e.g. 0.12); if large magnitude (> 60), treat as ms.
    const long long offsetMs = (std::abs(rawOffset) <= 60.0)
                                   ? std::llround(rawOffset * 1000.0)
                                   : std::llround(rawOffset);
    const float safeFallbackBpm = ClampBpm(fallbackBpm, 120.0f);

    ChartData data;
    data.version = 4;

    const auto& lines = root["judgeLineList"];
    float primaryBpm  = safeFallbackBpm;
    bool  gotPrimary  = false;

    std::vector<RawCandidateNote> allRawNotes;
    const int numLines = static_cast<int>(lines.size());

    for (int li = 0; li < numLines; ++li)
    {
        const auto& line = lines[static_cast<std::size_t>(li)];
        if (!line.is_object())
            continue;

        const float lineBpm = ClampBpm(JsonNumberOr(line, "bpm", safeFallbackBpm), safeFallbackBpm);
        if (!gotPrimary)
        {
            primaryBpm = lineBpm;
            gotPrimary = true;
        }

        const double unitMsScale = (formatVersion == 1)
                                       ? (1000.0 / static_cast<double>(lineBpm))
                                       : (1875.0 / static_cast<double>(lineBpm));

        // Extract gentle SV points from primary judge line speedEvents
        if (li == 0 && line.contains("speedEvents") && line["speedEvents"].is_array())
        {
            for (const auto& sev : line["speedEvents"])
            {
                if (!sev.is_object())
                    continue;
                const double st  = JsonNumberOr(sev, "startTime", 0.0);
                const double val = JsonNumberOr(sev, "value", 1.0);
                if (!std::isfinite(val) || val <= 0.05)
                    continue;
                const long long tMs = std::max<long long>(0, std::llround(st * unitMsScale) + offsetMs);
                if (tMs > kMaxChartTimeMs)
                    continue;
                SVPoint sv;
                sv.time   = static_cast<int>(tMs);
                sv.speed  = static_cast<float>(std::clamp(val / 2.0, 0.45, 2.20));
                sv.easing = "linear";
                data.svPoints.push_back(std::move(sv));
                if (data.svPoints.size() >= 256)
                    break;
            }
        }

        auto collectNoteArray = [&](const char* fieldName, bool isAbove) {
            if (!line.contains(fieldName) || !line[fieldName].is_array())
                return;
            for (const auto& n : line[fieldName])
            {
                if (!n.is_object())
                    continue;
                const int rawType = JsonIntOr(n, "type", 1);
                if (rawType < 1 || rawType > 4)
                    continue;
                if (JsonIntOr(n, "isFake", 0) != 0)
                    continue;

                const double rawTime = JsonNumberOr(n, "time", 0.0);
                const double rawHold = std::max(0.0, JsonNumberOr(n, "holdTime", 0.0));
                double       posX    = JsonNumberOr(n, "positionX", 0.0);
                if (formatVersion == 1 && std::abs(posX) > 20.0)
                    posX = posX / 1000.0;

                const long long startMs = std::max<long long>(0, std::llround(rawTime * unitMsScale) + offsetMs);
                if (startMs > kMaxChartTimeMs)
                    continue;
                const long long holdMs = std::max<long long>(0, std::llround(rawHold * unitMsScale));

                RawCandidateNote cand;
                cand.timeMs     = static_cast<int>(startMs);
                cand.durationMs = static_cast<int>(std::min<long long>(holdMs, kMaxChartTimeMs - startMs));
                cand.rawX       = posX;
                cand.lineIndex  = li;
                cand.above      = isAbove;
                // Map Phigros Official types (1=Tap, 2=Drag, 3=Hold, 4=Flick) -> unified kind (1=Tap, 2=Hold, 3=Flick, 4=Drag)
                if (rawType == 1)      cand.kind = 1;
                else if (rawType == 3) cand.kind = 2;
                else if (rawType == 4) cand.kind = 3;
                else                   cand.kind = 4;

                const float lineRatio = (numLines > 1)
                                            ? static_cast<float>(li) / static_cast<float>(numLines - 1)
                                            : 0.5f;
                const float wave      = 0.50f + 0.24f * std::sin(static_cast<float>(cand.timeMs) * 0.0024f +
                                                                 static_cast<float>(li) * 0.85f);
                cand.mouseYHint = std::clamp(0.55f * wave + 0.30f * lineRatio + (isAbove ? -0.06f : 0.08f), 0.0f, 1.0f);
                allRawNotes.push_back(cand);
            }
        };

        collectNoteArray("notesAbove", true);
        collectNoteArray("notesBelow", false);
    }

    data.timingPoints.push_back({ 0, primaryBpm, 4, 4 });
    AutoNormalizeCandidateX(allRawNotes, 0.0);

    std::stable_sort(allRawNotes.begin(), allRawNotes.end(),
                     [](const RawCandidateNote& a, const RawCandidateNote& b) {
                         if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                         return a.kind < b.kind;
                     });

    const double beatMs = 60000.0 / static_cast<double>(primaryBpm);
    DualZoneLaneRouter router;
    std::vector<RawCandidateNote> mouseCands;

    for (std::size_t i = 0; i < allRawNotes.size(); ++i)
    {
        const auto& c = allRawNotes[i];
        if (c.kind == 1 || c.kind == 2)
        {
            const bool isChord = (i + 1 < allRawNotes.size() && std::abs(allRawNotes[i + 1].timeMs - c.timeMs) <= 6) ||
                                 (i > 0 && std::abs(allRawNotes[i - 1].timeMs - c.timeMs) <= 6);
            const double beatPhase  = std::fmod(static_cast<double>(c.timeMs), beatMs * 4.0);
            const bool   onDownbeat = (beatPhase <= 30.0) || (beatMs * 4.0 - beatPhase <= 30.0);
            const bool   thumbAcc   = isChord && onDownbeat;

            if (!router.TryPlaceKeyboard(c.timeMs, (c.kind == 2) ? c.durationMs : 0, c.normX, thumbAcc, data.keyboardNotes))
            {
                mouseCands.push_back(c);
            }
        }
        else
        {
            mouseCands.push_back(c);
        }
    }

    BuildMouseNotesFromPhiraCandidates(mouseCands, data.mouseNotes);
    EnsureDualZoneBalance(data.keyboardNotes, data.mouseNotes);
    OptimizeMouseSpatialLayout(data.mouseNotes);
    BindCrossZoneLinks(data.keyboardNotes, data.mouseNotes);
    GenerateStageEventsForChart(data);

    if (!FinalizeAndValidateChart(data, primaryBpm))
        return std::nullopt;
    return data;
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. ConvertPhiraRpeJson
// ═════════════════════════════════════════════════════════════════════════════

std::optional<ChartData> ChartConverter::ConvertPhiraRpeJson(std::string_view jsonText,
                                                             ChartInfo* outMeta,
                                                             std::string* outLevelText)
{
    json root;
    try
    {
        root = json::parse(StripUtf8Bom(jsonText));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[ChartConverter] RPE JSON 解析失败: {}", e.what());
        return std::nullopt;
    }

    if (!root.is_object() || !root.contains("judgeLineList") || !root["judgeLineList"].is_array())
        return std::nullopt;
    if (!root.contains("BPMList") && !root.contains("META"))
        return std::nullopt;

    // Parse BPMList piecewise segments
    struct BpmSeg
    {
        double startBeat = 0.0;
        double bpm       = 120.0;
        double cumMs     = 0.0;
    };
    std::vector<BpmSeg> bpmSegs;
    if (root.contains("BPMList") && root["BPMList"].is_array())
    {
        for (const auto& item : root["BPMList"])
        {
            if (!item.is_object())
                continue;
            BpmSeg seg;
            if (item.contains("startTime"))
                seg.startBeat = std::max(0.0, ParseRpeBeatTriplet(item["startTime"]));
            seg.bpm = static_cast<double>(ClampBpm(JsonNumberOr(item, "bpm", 120.0), 120.0f));
            bpmSegs.push_back(seg);
        }
    }
    if (bpmSegs.empty())
        bpmSegs.push_back({ 0.0, 120.0, 0.0 });

    std::stable_sort(bpmSegs.begin(), bpmSegs.end(),
                     [](const BpmSeg& a, const BpmSeg& b) { return a.startBeat < b.startBeat; });
    {
        std::vector<BpmSeg> dedup;
        dedup.reserve(bpmSegs.size());
        for (const auto& s : bpmSegs)
        {
            if (!dedup.empty() && std::abs(dedup.back().startBeat - s.startBeat) < 1e-7)
                dedup.back() = s;
            else
                dedup.push_back(s);
        }
        if (dedup.front().startBeat > 0.0)
        {
            BpmSeg first    = dedup.front();
            first.startBeat = 0.0;
            dedup.insert(dedup.begin(), first);
        }
        dedup[0].cumMs = 0.0;
        for (std::size_t i = 1; i < dedup.size(); ++i)
        {
            const double dBeats = dedup[i].startBeat - dedup[i - 1].startBeat;
            dedup[i].cumMs      = dedup[i - 1].cumMs + dBeats * (60000.0 / dedup[i - 1].bpm);
        }
        bpmSegs = std::move(dedup);
    }

    double metaOffsetMs = 0.0;
    if (root.contains("META") && root["META"].is_object())
    {
        const auto& meta = root["META"];
        metaOffsetMs     = JsonNumberOr(meta, "offset", 0.0);

        const std::string levelStr = JsonStringOr(meta, "level", "");
        if (outLevelText != nullptr)
            *outLevelText = levelStr;

        if (outMeta != nullptr)
        {
            outMeta->version = 4;
            const std::string name       = JsonStringOr(meta, "name", "");
            const std::string composer   = JsonStringOr(meta, "composer", "");
            const std::string charter    = JsonStringOr(meta, "charter", "");
            const std::string song       = JsonStringOr(meta, "song", "");
            const std::string background = JsonStringOr(meta, "background", "");
            const std::string id         = JsonStringOr(meta, "id", "");

            if (!name.empty())       outMeta->title          = name;
            if (!composer.empty())   outMeta->artist         = composer;
            if (!charter.empty())    outMeta->charter        = charter;
            if (!song.empty())       outMeta->musicFile      = song;
            if (!background.empty())
            {
                outMeta->coverFile = background;
                if (outMeta->backgroundFile.empty())
                    outMeta->backgroundFile = background;
            }
            if (!id.empty())         outMeta->id             = id;
            outMeta->bpm    = static_cast<float>(bpmSegs.front().bpm);
            outMeta->offset = 0;
        }
    }
    else if (outLevelText != nullptr)
    {
        outLevelText->clear();
    }

    auto beatToMs = [&](double beat) -> int {
        const double safeBeat = std::max(0.0, beat);
        auto it = std::upper_bound(bpmSegs.begin(), bpmSegs.end(), safeBeat,
                                   [](double val, const BpmSeg& seg) { return val < seg.startBeat; });
        const std::size_t idx = (it == bpmSegs.begin()) ? 0 : static_cast<std::size_t>((it - bpmSegs.begin()) - 1);
        const double rawMs    = bpmSegs[idx].cumMs + (safeBeat - bpmSegs[idx].startBeat) * (60000.0 / bpmSegs[idx].bpm);
        const long long shifted = std::llround(rawMs - metaOffsetMs);
        return static_cast<int>(std::clamp<long long>(shifted, 0, kMaxChartTimeMs));
    };

    ChartData data;
    data.version = 4;
    for (const auto& seg : bpmSegs)
    {
        TimingPoint tp;
        tp.time               = beatToMs(seg.startBeat);
        tp.bpm                = static_cast<float>(seg.bpm);
        tp.timeSigNumerator   = 4;
        tp.timeSigDenominator = 4;
        data.timingPoints.push_back(tp);
    }

    const auto& lines    = root["judgeLineList"];
    const int   numLines = static_cast<int>(lines.size());
    std::vector<RawCandidateNote> allRawNotes;

    for (int li = 0; li < numLines; ++li)
    {
        const auto& line = lines[static_cast<std::size_t>(li)];
        if (!line.is_object() || !line.contains("notes") || !line["notes"].is_array())
            continue;

        for (const auto& n : line["notes"])
        {
            if (!n.is_object())
                continue;
            // Skip fake visual notes!
            if (JsonIntOr(n, "isFake", 0) != 0)
                continue;

            const int rpeType = JsonIntOr(n, "type", 1);
            if (rpeType < 1 || rpeType > 4)
                continue;

            const double startBeat = n.contains("startTime") ? ParseRpeBeatTriplet(n["startTime"]) : 0.0;
            const double endBeat   = n.contains("endTime") ? ParseRpeBeatTriplet(n["endTime"]) : startBeat;
            const int    startMs   = beatToMs(startBeat);
            const int    endMs     = std::max(startMs, beatToMs(std::max(startBeat, endBeat)));

            RawCandidateNote cand;
            cand.timeMs     = startMs;
            cand.durationMs = (rpeType == 2) ? std::max(60, endMs - startMs) : 0;
            cand.rawX       = JsonNumberOr(n, "positionX", 0.0);
            cand.kind       = rpeType; // RPE: 1=Tap, 2=Hold, 3=Flick, 4=Drag
            cand.lineIndex  = li;
            cand.above      = (JsonIntOr(n, "above", 1) == 1);

            const float lineRatio = (numLines > 1)
                                        ? static_cast<float>(li) / static_cast<float>(numLines - 1)
                                        : 0.5f;
            const float wave      = 0.50f + 0.25f * std::sin(static_cast<float>(startMs) * 0.0023f +
                                                             static_cast<float>(li) * 0.90f);
            cand.mouseYHint = std::clamp(0.55f * wave + 0.30f * lineRatio + (cand.above ? -0.05f : 0.07f), 0.0f, 1.0f);
            allRawNotes.push_back(cand);
        }
    }

    AutoNormalizeCandidateX(allRawNotes, 675.0);

    std::stable_sort(allRawNotes.begin(), allRawNotes.end(),
                     [](const RawCandidateNote& a, const RawCandidateNote& b) {
                         if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                         return a.kind < b.kind;
                     });

    const double baseBeatMs = 60000.0 / bpmSegs.front().bpm;
    DualZoneLaneRouter router;
    std::vector<RawCandidateNote> mouseCands;

    for (std::size_t i = 0; i < allRawNotes.size(); ++i)
    {
        const auto& c = allRawNotes[i];
        if (c.kind == 1 || c.kind == 2)
        {
            const bool isChord = (i + 1 < allRawNotes.size() && std::abs(allRawNotes[i + 1].timeMs - c.timeMs) <= 6) ||
                                 (i > 0 && std::abs(allRawNotes[i - 1].timeMs - c.timeMs) <= 6);
            const double beatPhase  = std::fmod(static_cast<double>(c.timeMs), baseBeatMs * 4.0);
            const bool   onDownbeat = (beatPhase <= 30.0) || (baseBeatMs * 4.0 - beatPhase <= 30.0);
            const bool   thumbAcc   = isChord && onDownbeat;

            if (!router.TryPlaceKeyboard(c.timeMs, (c.kind == 2) ? c.durationMs : 0, c.normX, thumbAcc, data.keyboardNotes))
            {
                mouseCands.push_back(c);
            }
        }
        else
        {
            mouseCands.push_back(c);
        }
    }

    BuildMouseNotesFromPhiraCandidates(mouseCands, data.mouseNotes);
    EnsureDualZoneBalance(data.keyboardNotes, data.mouseNotes);
    OptimizeMouseSpatialLayout(data.mouseNotes);
    BindCrossZoneLinks(data.keyboardNotes, data.mouseNotes);
    GenerateStageEventsForChart(data);

    if (!FinalizeAndValidateChart(data, static_cast<float>(bpmSegs.front().bpm)))
        return std::nullopt;

    if (outMeta != nullptr)
    {
        std::string lvStr;
        if (root.contains("META") && root["META"].is_object())
            lvStr = JsonStringOr(root["META"], "level", "");
        DifficultyInfo di;
        di.name      = ExtractDiffNameFromLevelText(lvStr, "IN");
        di.level     = EstimateChartLevel(data, ParseLevelFromText(lvStr, 0.0f));
        di.chartFile = "in.json";
        PopulateDifficultyCounts(di, data);
        if (outMeta->difficulties.empty())
            outMeta->difficulties.push_back(std::move(di));
    }

    return data;
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. ConvertAnyPhiraJson
// ═════════════════════════════════════════════════════════════════════════════

std::optional<ChartData> ChartConverter::ConvertAnyPhiraJson(std::string_view jsonText,
                                                             float fallbackBpm,
                                                             ChartInfo* outMeta,
                                                             std::string* outLevelText)
{
    json probe;
    try
    {
        probe = json::parse(StripUtf8Bom(jsonText));
    }
    catch (...)
    {
        return std::nullopt;
    }

    if (!probe.is_object())
        return std::nullopt;

    if (probe.contains("BPMList") || probe.contains("META"))
        return ConvertPhiraRpeJson(jsonText, outMeta, outLevelText);

    if (probe.contains("judgeLineList") || probe.contains("formatVersion"))
    {
        auto res = ConvertPhigrosOfficialJson(jsonText, fallbackBpm);
        if (res.has_value() && outMeta != nullptr && !res->timingPoints.empty())
        {
            outMeta->version = 4;
            outMeta->bpm     = res->timingPoints.front().bpm;
        }
        return res;
    }

    return std::nullopt;
}

namespace
{

std::vector<std::string_view> SplitLinesSv(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos)
        {
            std::string_view tail = text.substr(pos);
            if (!tail.empty() && tail.back() == '\r')
                tail.remove_suffix(1);
            lines.push_back(tail);
            break;
        }
        std::string_view line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back(line);
        pos = nl + 1;
        if (pos == text.size())
            break;
    }
    return lines;
}

std::vector<std::string_view> SplitCharSv(std::string_view sv, char delim)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= sv.size())
    {
        const std::size_t p = sv.find(delim, start);
        if (p == std::string_view::npos)
        {
            parts.push_back(sv.substr(start));
            break;
        }
        parts.push_back(sv.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

std::string NormalizeRelAssetName(std::string_view raw)
{
    std::string s = StripQuotes(raw);
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && (s.front() == '/' || s.front() == '.'))
    {
        if (s.rfind("./", 0) == 0)
            s.erase(0, 2);
        else if (s.front() == '/')
            s.erase(0, 1);
        else
            break;
    }
    const fs::path p(s);
    if (p.is_absolute() || p.has_root_name() || s.find(':') != std::string::npos)
        return p.filename().generic_string();
    for (const auto& part : p)
    {
        if (part == "..")
            return p.filename().generic_string();
    }
    return s;
}

std::string MakeSafeSlug(std::string_view raw, std::string_view fallbackPrefix = "chart")
{
    std::string ascii;
    ascii.reserve(raw.size());
    bool hadNonAscii = false;
    bool prevUnder   = false;
    for (unsigned char uc : raw)
    {
        if (uc >= 128U)
        {
            hadNonAscii = true;
            continue;
        }
        if (std::isalnum(uc) != 0)
        {
            ascii.push_back(static_cast<char>(std::tolower(uc)));
            prevUnder = false;
        }
        else if ((uc == '_' || uc == '-' || uc == ' ' || uc == '.') && !ascii.empty() && !prevUnder)
        {
            ascii.push_back('_');
            prevUnder = true;
        }
    }
    while (!ascii.empty() && ascii.back() == '_')
        ascii.pop_back();

    if (ascii.size() > 36)
        ascii.resize(36);
    while (!ascii.empty() && ascii.back() == '_')
        ascii.pop_back();

    const bool wasEmptyAscii = ascii.empty();
    if (wasEmptyAscii)
        ascii = std::string(fallbackPrefix);

    if (hadNonAscii || (wasEmptyAscii && !raw.empty()))
    {
        const std::string hash8 = sakura::utils::ComputeSha256Hex(raw).substr(0, 8);
        ascii += "_" + hash8;
    }
    return ascii;
}

bool ReadWholeFileBinary(const fs::path& p, std::string& outBytes, std::uintmax_t maxBytes = 64ULL * 1024ULL * 1024ULL)
{
    std::error_code ec;
    const auto sz = fs::file_size(p, ec);
    if (ec || sz > maxBytes)
        return false;
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    outBytes.resize(static_cast<std::size_t>(sz));
    if (sz > 0 && !in.read(outBytes.data(), static_cast<std::streamsize>(sz)))
        return false;
    return true;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 5. ConvertOsuBeatmapText
// ═════════════════════════════════════════════════════════════════════════════

std::optional<ChartData> ChartConverter::ConvertOsuBeatmapText(std::string_view osuText,
                                                               ChartInfo* outMeta,
                                                               DifficultyInfo* outDiffInfo)
{
    const std::string_view cleanText = StripUtf8Bom(osuText);
    if (cleanText.empty())
        return std::nullopt;

    const auto lines = SplitLinesSv(cleanText);
    bool sawOsuSignature = (cleanText.find("osu file format") != std::string_view::npos);

    std::string section;
    std::string audioFilename;
    int         previewTimeMs    = 0;
    int         mode             = 0;
    std::string titleAscii;
    std::string titleUnicode;
    std::string artistAscii;
    std::string artistUnicode;
    std::string creator;
    std::string versionName;
    std::string source;
    std::string beatmapSetId;
    std::string beatmapId;
    std::vector<std::string> tags;

    double circleSize       = 4.0;
    double overallDiff      = 5.0;
    double sliderMultiplier = 1.4;
    std::string bgFile;
    std::string videoFile;

    struct OsuTimingLine
    {
        double timeMs      = 0.0;
        double beatLength  = 500.0;
        int    meter       = 4;
        int    uninherited = 1;
    };
    std::vector<OsuTimingLine> osuTimings;

    struct OsuHitObj
    {
        double x        = 256.0;
        double y        = 192.0;
        int    timeMs   = 0;
        int    objType  = 1;
        int    hitSound = 0;
        std::vector<std::string> extraFields;
    };
    std::vector<OsuHitObj> hitObjs;

    for (std::string_view rawLine : lines)
    {
        const std::string trimmed = TrimSv(rawLine);
        if (trimmed.empty() || trimmed.rfind("//", 0) == 0)
            continue;

        if (trimmed.front() == '[' && trimmed.back() == ']')
        {
            section = ToLowerAscii(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            if (section == "general" || section == "metadata" || section == "difficulty" ||
                section == "timingpoints" || section == "hitobjects" || section == "events")
            {
                sawOsuSignature = true;
            }
            continue;
        }

        if (section == "general" || section == "metadata" || section == "difficulty")
        {
            const std::size_t colon = trimmed.find(':');
            if (colon == std::string::npos)
                continue;
            const std::string key = ToLowerAscii(TrimSv(std::string_view(trimmed).substr(0, colon)));
            const std::string val = TrimSv(std::string_view(trimmed).substr(colon + 1));

            if (section == "general")
            {
                if (key == "audiofilename")
                    audioFilename = NormalizeRelAssetName(val);
                else if (key == "previewtime")
                {
                    int pt = 0;
                    if (ParseIntSafe(val, pt))
                        previewTimeMs = std::max(0, pt);
                }
                else if (key == "mode")
                {
                    int m = 0;
                    if (ParseIntSafe(val, m))
                        mode = std::clamp(m, 0, 3);
                }
            }
            else if (section == "metadata")
            {
                if (key == "title")             titleAscii    = val;
                else if (key == "titleunicode") titleUnicode  = val;
                else if (key == "artist")       artistAscii   = val;
                else if (key == "artistunicode")artistUnicode = val;
                else if (key == "creator")      creator       = val;
                else if (key == "version")      versionName   = val;
                else if (key == "source")       source        = val;
                else if (key == "beatmapsetid") beatmapSetId  = val;
                else if (key == "beatmapid")    beatmapId     = val;
                else if (key == "tags")
                {
                    std::istringstream iss(val);
                    std::string tok;
                    while (iss >> tok && tags.size() < 24)
                        tags.push_back(tok);
                }
            }
            else if (section == "difficulty")
            {
                if (key == "circlesize")        ParseDoubleSafe(val, circleSize);
                else if (key == "overalldifficulty") ParseDoubleSafe(val, overallDiff);
                else if (key == "slidermultiplier")
                {
                    double sm = 1.4;
                    if (ParseDoubleSafe(val, sm) && sm > 0.05)
                        sliderMultiplier = std::clamp(sm, 0.2, 6.0);
                }
            }
        }
        else if (section == "events")
        {
            const auto parts = SplitCharSv(trimmed, ',');
            if (parts.size() >= 3)
            {
                const std::string evType = ToLowerAscii(TrimSv(parts[0]));
                if ((evType == "0" || evType == "background") && bgFile.empty())
                    bgFile = NormalizeRelAssetName(parts[2]);
                else if ((evType == "1" || evType == "video") && videoFile.empty())
                    videoFile = NormalizeRelAssetName(parts[2]);
            }
        }
        else if (section == "timingpoints")
        {
            const auto parts = SplitCharSv(trimmed, ',');
            if (parts.size() < 2)
                continue;
            double tMs = 0.0;
            double beatLen = 500.0;
            if (!ParseDoubleSafe(parts[0], tMs) || !ParseDoubleSafe(parts[1], beatLen))
                continue;
            int meter = 4;
            if (parts.size() >= 3)
                ParseIntSafe(parts[2], meter);
            int uninherited = (beatLen > 0.0) ? 1 : 0;
            if (parts.size() >= 7)
                ParseIntSafe(parts[6], uninherited);
            osuTimings.push_back({ tMs, beatLen, std::max(1, meter), uninherited });
        }
        else if (section == "hitobjects")
        {
            const auto parts = SplitCharSv(trimmed, ',');
            if (parts.size() < 5)
                continue;
            double hx = 256.0, hy = 192.0, ht = 0.0;
            int    hType = 1, hSound = 0;
            if (!ParseDoubleSafe(parts[0], hx) || !ParseDoubleSafe(parts[1], hy) ||
                !ParseDoubleSafe(parts[2], ht) || !ParseIntSafe(parts[3], hType))
            {
                continue;
            }
            ParseIntSafe(parts[4], hSound);

            OsuHitObj obj;
            obj.x        = hx;
            obj.y        = hy;
            obj.timeMs   = std::clamp(static_cast<int>(std::llround(ht)), 0, kMaxChartTimeMs);
            obj.objType  = hType;
            obj.hitSound = hSound;
            for (std::size_t fi = 5; fi < parts.size(); ++fi)
                obj.extraFields.emplace_back(parts[fi]);
            hitObjs.push_back(std::move(obj));
        }
    }

    if (!sawOsuSignature)
        return std::nullopt;

    std::stable_sort(osuTimings.begin(), osuTimings.end(),
                     [](const OsuTimingLine& a, const OsuTimingLine& b) {
                         if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                         return a.uninherited > b.uninherited;
                     });

    ChartData data;
    data.version = 4;
    float primaryBpm = 120.0f;
    bool  gotPrimaryBpm = false;

    for (const auto& tl : osuTimings)
    {
        const int clampedT = std::clamp(static_cast<int>(std::llround(tl.timeMs)), 0, kMaxChartTimeMs);
        if (tl.uninherited != 0 && tl.beatLength > 0.01)
        {
            const float bpm = ClampBpm(60000.0 / tl.beatLength, 120.0f);
            if (!gotPrimaryBpm)
            {
                primaryBpm    = bpm;
                gotPrimaryBpm = true;
            }
            data.timingPoints.push_back({ clampedT, bpm, tl.meter, 4 });
        }
        else if (tl.uninherited == 0 && tl.beatLength < -0.01 && data.svPoints.size() < 512)
        {
            const float speed = static_cast<float>(std::clamp(-100.0 / tl.beatLength, 0.25, 3.50));
            data.svPoints.push_back({ clampedT, speed, "linear" });
        }
    }
    if (data.timingPoints.empty())
        data.timingPoints.push_back({ 0, 120.0f, 4, 4 });

    auto queryTimingAt = [&](double timeMs) -> std::tuple<double, double, double> {
        double beatLen   = 60000.0 / static_cast<double>(primaryBpm);
        double svMult    = 1.0;
        double redAnchor = 0.0;
        for (const auto& tl : osuTimings)
        {
            if (tl.timeMs > timeMs + 1.0)
                break;
            if (tl.uninherited != 0 && tl.beatLength > 0.01)
            {
                beatLen   = std::clamp(tl.beatLength, 62.5, 6000.0);
                redAnchor = tl.timeMs;
                svMult    = 1.0;
            }
            else if (tl.uninherited == 0 && tl.beatLength < -0.01)
            {
                svMult = std::clamp(-100.0 / tl.beatLength, 0.25, 4.0);
            }
        }
        return { beatLen, svMult, redAnchor };
    };

    std::stable_sort(hitObjs.begin(), hitObjs.end(),
                     [](const OsuHitObj& a, const OsuHitObj& b) { return a.timeMs < b.timeMs; });

    if (mode == 3) // osu!mania (4K..8K+)
    {
        const int keyCount = std::clamp(static_cast<int>(std::lround(circleSize)), 1, 18);
        DualZoneLaneRouter router;
        std::vector<MouseNote> rawMouse;
        int lastMouseTime = -10000;

        for (std::size_t i = 0; i < hitObjs.size(); ++i)
        {
            const auto& h   = hitObjs[i];
            const int   col = std::clamp(static_cast<int>(std::floor(h.x * static_cast<double>(keyCount) / 512.0)),
                                         0, keyCount - 1);
            int holdDur = 0;
            if ((h.objType & 128) != 0 && !h.extraFields.empty())
            {
                const std::string_view ef = h.extraFields[0];
                const std::size_t colonPos = ef.find(':');
                int endMs = h.timeMs;
                if (ParseIntSafe(ef.substr(0, colonPos), endMs))
                    holdDur = std::max(0, endMs - h.timeMs);
            }

            const float normX  = (keyCount > 1) ? (static_cast<float>(col) / static_cast<float>(keyCount - 1)) : 0.5f;
            const float mouseY = std::clamp(0.50f + 0.27f * std::sin(static_cast<float>(h.timeMs) * 0.0021f +
                                                                     static_cast<float>(col) * 0.75f),
                                            0.0f, 1.0f);

            auto pushMouse = [&]() {
                if (h.timeMs - lastMouseTime <= 10)
                    return;
                if (holdDur >= 140)
                    rawMouse.push_back(MakeRawMouseSlider(h.timeMs, std::min(holdDur, 1200), normX, mouseY, 1.0f - normX, 1.0f - mouseY));
                else
                    rawMouse.push_back(MakeRawMouseCircle(h.timeMs, normX, mouseY));
                lastMouseTime = h.timeMs;
            };

            // Count chord size at this timestamp
            int chordSize = 1;
            for (std::size_t j = i + 1; j < hitObjs.size() && (hitObjs[j].timeMs - h.timeMs) <= 6; ++j)
                ++chordSize;

            // Center thumb column in 5K / 7K
            if ((keyCount == 5 && col == 2) || (keyCount == 7 && col == 3))
            {
                if (!router.TryPlaceSpecificLane(h.timeMs, holdDur, kThumbLaneIndex, data.keyboardNotes))
                    pushMouse();
                continue;
            }

            // Upper melody columns in 6K..8K+ -> route off-hand melodies to mouse when available, else fold into 4K
            if (keyCount >= 6 && col >= 4)
            {
                if (h.timeMs - lastMouseTime >= 150)
                {
                    pushMouse();
                }
                else
                {
                    const int folded = std::clamp(2 + ((col - 4) % 2), 0, 3);
                    if (!router.TryPlaceSpecificLane(h.timeMs, holdDur, folded, data.keyboardNotes))
                        if (!router.TryPlaceKeyboard(h.timeMs, holdDur, normX, false, data.keyboardNotes))
                            pushMouse();
                }
                continue;
            }

            const int mappedLane = [&]() -> int {
                if (keyCount <= 4) return std::clamp(col, 0, 3);
                if (keyCount == 5) return (col < 2) ? col : std::clamp(col - 1, 0, 3);
                if (keyCount == 7) return (col < 3) ? std::clamp(col, 0, 2) : 3;
                return std::clamp(col, 0, 3);
            }();

            const bool isFinishAccent = (h.hitSound & (2 | 8)) != 0; // HitWhistle / HitFinish
            if ((chordSize >= 3 || isFinishAccent) && (h.timeMs - lastMouseTime >= 260))
            {
                // Keep keyboard note AND emit cross-zone simultaneous mouse accent!
                if (!router.TryPlaceSpecificLane(h.timeMs, holdDur, mappedLane, data.keyboardNotes))
                    router.TryPlaceKeyboard(h.timeMs, holdDur, normX, true, data.keyboardNotes);
                pushMouse();
                continue;
            }

            if (!router.TryPlaceSpecificLane(h.timeMs, holdDur, mappedLane, data.keyboardNotes))
            {
                if (!router.TryPlaceKeyboard(h.timeMs, holdDur, normX, chordSize >= 2, data.keyboardNotes))
                    pushMouse();
            }
        }

        EnsureDualZoneBalance(data.keyboardNotes, rawMouse);
        data.mouseNotes = std::move(rawMouse);
    }
    else // Mode 0 (osu!standard) and non-mania fallback
    {
        DualZoneLaneRouter kbRouter;
        int lastMouseTime  = -10000;
        int lastAutoKbTime = -10000;
        int prevAutoLane   = -1;

        for (std::size_t i = 0; i < hitObjs.size(); ++i)
        {
            const auto& h = hitObjs[i];
            const auto [beatLen, svMult, redAnchor] = queryTimingAt(h.timeMs);
            const float nx = std::clamp(static_cast<float>(h.x / 512.0), 0.0f, 1.0f);
            const float ny = std::clamp(static_cast<float>(h.y / 384.0), 0.0f, 1.0f);

            int sliderDurForKb = 0;
            if ((h.objType & 2) != 0) // Slider
            {
                const std::string curveSpec = !h.extraFields.empty() ? h.extraFields[0] : "";
                int slides = 1;
                if (h.extraFields.size() >= 2)
                    ParseIntSafe(h.extraFields[1], slides);
                slides = std::clamp(slides, 1, 16);
                double pixelLen = 100.0;
                if (h.extraFields.size() >= 3)
                    ParseDoubleSafe(h.extraFields[2], pixelLen);

                const double pxPerBeat = std::max(20.0, sliderMultiplier * 100.0 * svMult);
                const int maxAvailDur  = std::max(80, kMaxChartTimeMs - h.timeMs);
                const int durMs = std::clamp(
                    static_cast<int>(std::llround((std::max(12.0, pixelLen) * static_cast<double>(slides) / pxPerBeat) * beatLen)),
                    80, std::min(3500, maxAvailDur));
                sliderDurForKb = durMs;

                if (h.timeMs - lastMouseTime > 10)
                {
                    MouseNote mn = MakeRawMouseCircle(h.timeMs, nx, ny);
                    mn.type           = NoteType::Slider;
                    mn.sliderDuration = durMs;

                    std::vector<std::pair<float, float>> pts;
                    const auto tokens = SplitCharSv(curveSpec, '|');
                    for (std::size_t ti = 1; ti < tokens.size(); ++ti)
                    {
                        const auto xy = SplitCharSv(tokens[ti], ':');
                        if (xy.size() < 2)
                            continue;
                        double cx = 256.0, cy = 192.0;
                        if (!ParseDoubleSafe(xy[0], cx) || !ParseDoubleSafe(xy[1], cy))
                            continue;
                        const float wx = std::clamp(kMouseMinX + (kMouseMaxX - kMouseMinX) * std::clamp(static_cast<float>(cx / 512.0), 0.0f, 1.0f),
                                                    kSliderMinBound, kSliderMaxBound);
                        const float wy = std::clamp(kMouseMinY + (kMouseMaxY - kMouseMinY) * std::clamp(static_cast<float>(cy / 384.0), 0.0f, 1.0f),
                                                    kSliderMinBound, kSliderMaxBound);
                        if (!pts.empty() && std::hypot(wx - pts.back().first, wy - pts.back().second) < 0.015f)
                            continue;
                        pts.emplace_back(wx, wy);
                    }
                    if (pts.size() > 8)
                    {
                        std::vector<std::pair<float, float>> down;
                        down.reserve(8);
                        for (std::size_t k = 0; k < 8; ++k)
                        {
                            const std::size_t srcIdx = (k * (pts.size() - 1)) / 7;
                            down.push_back(pts[srcIdx]);
                        }
                        pts = std::move(down);
                    }
                    if ((slides % 2 == 0) && !pts.empty())
                        pts.emplace_back(mn.x, mn.y);
                    if (pts.empty())
                    {
                        const float ex = std::clamp(mn.x + ((mn.x < 0.5f) ? 0.16f : -0.16f), kMouseMinX, kMouseMaxX);
                        const float ey = std::clamp(mn.y + ((mn.y < 0.5f) ? 0.12f : -0.12f), kMouseMinY, kMouseMaxY);
                        pts.emplace_back(0.5f * (mn.x + ex), 0.5f * (mn.y + ey));
                        pts.emplace_back(ex, ey);
                    }
                    mn.sliderPath = std::move(pts);
                    data.mouseNotes.push_back(std::move(mn));
                    lastMouseTime = h.timeMs;
                }
            }
            else if ((h.objType & 8) != 0) // Spinner
            {
                int endMs = h.timeMs + 400;
                if (!h.extraFields.empty())
                    ParseIntSafe(h.extraFields[0], endMs);
                const int spinDur = std::clamp(endMs - h.timeMs, 120, std::max(120, std::min(3500, kMaxChartTimeMs - h.timeMs)));
                if (!kbRouter.TryPlaceSpecificLane(h.timeMs, spinDur, kThumbLaneIndex, data.keyboardNotes))
                    kbRouter.TryPlaceKeyboard(h.timeMs, spinDur, 0.5f, true, data.keyboardNotes);
                lastAutoKbTime = h.timeMs;
                continue;
            }
            else // HitCircle
            {
                if (h.timeMs - lastMouseTime > 10)
                {
                    data.mouseNotes.push_back(MakeRawMouseCircle(h.timeMs, nx, ny));
                    lastMouseTime = h.timeMs;
                }
            }

            // Generate complementary on-beat 4K left-hand rhythm accompaniment
            const double relBeats      = (static_cast<double>(h.timeMs) - redAnchor) / std::max(60.0, beatLen);
            const double nearestHalf   = std::round(relBeats * 2.0) * 0.5;
            const double errMs         = std::abs(relBeats - nearestHalf) * beatLen;
            const bool   isNewCombo    = (h.objType & 4) != 0;
            const bool   isWholeBeat   = std::abs(relBeats - std::round(relBeats)) * beatLen <= 40.0;
            const long long beatNumber = std::llround(std::abs(relBeats));
            const bool   isDownbeat    = isWholeBeat && (beatNumber % 4 == 0);

            if ((errMs <= 40.0 || isNewCombo || data.keyboardNotes.empty()) && (h.timeMs - lastAutoKbTime >= 115))
            {
                int lane = std::clamp(static_cast<int>(nx * 4.0f), 0, 3);
                if (lane == prevAutoLane)
                    lane = (lane + 1 + static_cast<int>(i % 2)) % 4;
                const int kbDur = (sliderDurForKb >= 240 && isWholeBeat) ? std::min(sliderDurForKb, 750) : 0;
                if (isDownbeat && isNewCombo)
                {
                    if (!kbRouter.TryPlaceSpecificLane(h.timeMs, kbDur, kThumbLaneIndex, data.keyboardNotes))
                        kbRouter.TryPlaceKeyboard(h.timeMs, kbDur, nx, false, data.keyboardNotes);
                }
                else
                {
                    if (!kbRouter.TryPlaceSpecificLane(h.timeMs, kbDur, lane, data.keyboardNotes))
                        kbRouter.TryPlaceKeyboard(h.timeMs, kbDur, nx, false, data.keyboardNotes);
                }
                prevAutoLane   = lane;
                lastAutoKbTime = h.timeMs;
            }
        }
    }

    OptimizeMouseSpatialLayout(data.mouseNotes);
    BindCrossZoneLinks(data.keyboardNotes, data.mouseNotes);
    GenerateStageEventsForChart(data);

    if (!FinalizeAndValidateChart(data, primaryBpm))
        return std::nullopt;

    const float odLevelHint = (overallDiff > 0.0)
                                  ? static_cast<float>(std::clamp(overallDiff * 1.35, 1.0, 15.0))
                                  : 0.0f;
    const float densityLv   = EstimateChartLevel(data, 0.0f);
    const float blendedLv   = (odLevelHint > 0.0f) ? (0.45f * odLevelHint + 0.55f * densityLv) : densityLv;

    DifficultyInfo di;
    di.name      = !versionName.empty() ? versionName : ((mode == 3) ? "Mania" : "Standard");
    di.level     = EstimateChartLevel(data, blendedLv);
    di.chartFile = MakeSafeSlug(di.name, "normal") + ".json";
    PopulateDifficultyCounts(di, data);

    if (outDiffInfo != nullptr)
        *outDiffInfo = di;

    if (outMeta != nullptr)
    {
        outMeta->version = 4;
        const std::string chosenTitle  = !titleUnicode.empty() ? titleUnicode : titleAscii;
        const std::string chosenArtist = !artistUnicode.empty() ? artistUnicode : artistAscii;
        if (!chosenTitle.empty())   outMeta->title       = chosenTitle;
        else if (outMeta->title.empty()) outMeta->title  = "Unknown";
        if (!chosenArtist.empty())  outMeta->artist      = chosenArtist;
        else if (outMeta->artist.empty()) outMeta->artist = "Unknown";
        if (!creator.empty())       outMeta->charter     = creator;
        else if (outMeta->charter.empty()) outMeta->charter = "Unknown";
        if (!source.empty())        outMeta->source      = source;
        if (!tags.empty())          outMeta->tags        = tags;
        if (!audioFilename.empty()) outMeta->musicFile   = audioFilename;
        if (!bgFile.empty())
        {
            outMeta->coverFile      = bgFile;
            outMeta->backgroundFile = bgFile;
        }
        if (!videoFile.empty())     outMeta->videoFile   = videoFile;
        outMeta->previewTime = previewTimeMs;
        outMeta->bpm         = primaryBpm;
        outMeta->offset      = 0;
        if (!beatmapSetId.empty() && beatmapSetId != "-1" && beatmapSetId != "0")
            outMeta->id = "osu_" + MakeSafeSlug(beatmapSetId, "set");
        else if (!beatmapId.empty() && beatmapId != "-1" && beatmapId != "0" && outMeta->id.empty())
            outMeta->id = "osu_" + MakeSafeSlug(beatmapId, "map");
    }

    return data;
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. ConvertStepManiaSmText
// ═════════════════════════════════════════════════════════════════════════════

bool ChartConverter::ConvertStepManiaSmText(
    std::string_view smText,
    ChartInfo& outMeta,
    std::vector<std::pair<DifficultyInfo, ChartData>>& outDifficulties)
{
    outDifficulties.clear();
    const std::string_view rawNoBom = StripUtf8Bom(smText);
    if (rawNoBom.empty())
        return false;

    // Strip '//' line comments while preserving newlines
    std::string cleaned;
    cleaned.reserve(rawNoBom.size());
    for (std::string_view line : SplitLinesSv(rawNoBom))
    {
        const std::size_t commentPos = line.find("//");
        const std::string_view kept  = (commentPos == std::string_view::npos) ? line : line.substr(0, commentPos);
        cleaned.append(kept);
        cleaned.push_back('\n');
    }

    std::string title;
    std::string titleTranslit;
    std::string artist;
    std::string artistTranslit;
    std::string credit;
    std::string music;
    std::string banner;
    std::string background;
    double      sampleStartSec = 0.0;
    double      offsetSec      = 0.0;
    std::string bpmsRaw;
    std::string stopsRaw;
    std::vector<std::string> notesBlocks;

    std::size_t pos = 0;
    while (pos < cleaned.size())
    {
        const std::size_t hashPos = cleaned.find('#', pos);
        if (hashPos == std::string::npos)
            break;
        const std::size_t colonPos = cleaned.find(':', hashPos + 1);
        if (colonPos == std::string::npos)
            break;
        std::size_t semiPos = cleaned.find(';', colonPos + 1);
        if (semiPos == std::string::npos)
            semiPos = cleaned.size();

        const std::string tagName = [&]() {
            std::string t = TrimSv(std::string_view(cleaned).substr(hashPos + 1, colonPos - hashPos - 1));
            for (char& c : t)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return t;
        }();
        const std::string_view tagVal = std::string_view(cleaned).substr(colonPos + 1, semiPos - colonPos - 1);

        if (tagName == "TITLE")               title          = TrimSv(tagVal);
        else if (tagName == "TITLETRANSLIT")  titleTranslit  = TrimSv(tagVal);
        else if (tagName == "ARTIST")         artist         = TrimSv(tagVal);
        else if (tagName == "ARTISTTRANSLIT") artistTranslit = TrimSv(tagVal);
        else if (tagName == "CREDIT")         credit         = TrimSv(tagVal);
        else if (tagName == "MUSIC")          music          = NormalizeRelAssetName(tagVal);
        else if (tagName == "BANNER")         banner         = NormalizeRelAssetName(tagVal);
        else if (tagName == "BACKGROUND")     background     = NormalizeRelAssetName(tagVal);
        else if (tagName == "SAMPLESTART")    ParseDoubleSafe(tagVal, sampleStartSec);
        else if (tagName == "OFFSET")         ParseDoubleSafe(tagVal, offsetSec);
        else if (tagName == "BPMS")           bpmsRaw        = std::string(tagVal);
        else if (tagName == "STOPS")          stopsRaw       = std::string(tagVal);
        else if (tagName == "NOTES")          notesBlocks.emplace_back(tagVal);

        pos = (semiPos < cleaned.size()) ? (semiPos + 1) : cleaned.size();
    }

    if (notesBlocks.empty())
        return false;

    struct SmBpmSeg
    {
        double beat   = 0.0;
        double bpm    = 120.0;
        double cumSec = 0.0;
    };
    std::vector<SmBpmSeg> bpmSegs;
    for (std::string_view pairSv : SplitCharSv(bpmsRaw, ','))
    {
        const std::size_t eq = pairSv.find('=');
        if (eq == std::string_view::npos)
            continue;
        double b = 0.0, v = 120.0;
        if (ParseDoubleSafe(pairSv.substr(0, eq), b) && ParseDoubleSafe(pairSv.substr(eq + 1), v))
            bpmSegs.push_back({ std::max(0.0, b), static_cast<double>(ClampBpm(v, 120.0f)), 0.0 });
    }
    if (bpmSegs.empty())
        bpmSegs.push_back({ 0.0, 120.0, 0.0 });
    std::stable_sort(bpmSegs.begin(), bpmSegs.end(),
                     [](const SmBpmSeg& a, const SmBpmSeg& b) { return a.beat < b.beat; });
    {
        std::vector<SmBpmSeg> dedup;
        for (const auto& s : bpmSegs)
        {
            if (!dedup.empty() && std::abs(dedup.back().beat - s.beat) < 1e-7)
                dedup.back() = s;
            else
                dedup.push_back(s);
        }
        if (dedup.front().beat > 0.0)
        {
            SmBpmSeg z = dedup.front();
            z.beat     = 0.0;
            dedup.insert(dedup.begin(), z);
        }
        dedup[0].cumSec = 0.0;
        for (std::size_t i = 1; i < dedup.size(); ++i)
            dedup[i].cumSec = dedup[i - 1].cumSec + (dedup[i].beat - dedup[i - 1].beat) * (60.0 / dedup[i - 1].bpm);
        bpmSegs = std::move(dedup);
    }

    struct SmStop
    {
        double beat = 0.0;
        double sec  = 0.0;
    };
    std::vector<SmStop> stops;
    for (std::string_view pairSv : SplitCharSv(stopsRaw, ','))
    {
        const std::size_t eq = pairSv.find('=');
        if (eq == std::string_view::npos)
            continue;
        double b = 0.0, s = 0.0;
        if (ParseDoubleSafe(pairSv.substr(0, eq), b) && ParseDoubleSafe(pairSv.substr(eq + 1), s) && s > 0.0)
            stops.push_back({ std::max(0.0, b), std::min(s, 30.0) });
    }

    auto beatToMs = [&](double beat) -> int {
        const double safeBeat = std::max(0.0, beat);
        auto it = std::upper_bound(bpmSegs.begin(), bpmSegs.end(), safeBeat,
                                   [](double v, const SmBpmSeg& seg) { return v < seg.beat; });
        const std::size_t idx = (it == bpmSegs.begin()) ? 0 : static_cast<std::size_t>((it - bpmSegs.begin()) - 1);
        double sec = bpmSegs[idx].cumSec + (safeBeat - bpmSegs[idx].beat) * (60.0 / bpmSegs[idx].bpm);
        for (const auto& st : stops)
        {
            if (safeBeat > st.beat + 1e-6)
                sec += st.sec;
        }
        const long long ms = std::llround((sec - offsetSec) * 1000.0);
        return static_cast<int>(std::clamp<long long>(ms, 0, kMaxChartTimeMs));
    };

    outMeta.version     = 4;
    outMeta.title       = !title.empty() ? title : (!titleTranslit.empty() ? titleTranslit : "StepMania Chart");
    outMeta.artist      = !artist.empty() ? artist : (!artistTranslit.empty() ? artistTranslit : "Unknown");
    outMeta.charter     = !credit.empty() ? credit : "Unknown";
    if (!music.empty())      outMeta.musicFile      = music;
    if (!banner.empty())     outMeta.coverFile      = banner;
    else if (!background.empty()) outMeta.coverFile = background;
    if (!background.empty()) outMeta.backgroundFile = background;
    else if (!banner.empty()) outMeta.backgroundFile = banner;
    outMeta.previewTime = std::max(0, static_cast<int>(std::llround(sampleStartSec * 1000.0)));
    outMeta.bpm         = static_cast<float>(bpmSegs.front().bpm);
    outMeta.offset      = 0;
    if (outMeta.id.empty())
        outMeta.id = MakeSafeSlug(outMeta.title, "sm_chart");

    // Prefer dance-single if present; otherwise allow other 4+ column step charts
    bool hasDanceSingle = false;
    for (const auto& nb : notesBlocks)
    {
        const auto fields = SplitCharSv(nb, ':');
        if (fields.size() >= 6 && ToLowerAscii(TrimSv(fields[0])) == "dance-single")
        {
            hasDanceSingle = true;
            break;
        }
    }

    std::vector<std::string> usedChartFiles;
    for (const auto& nb : notesBlocks)
    {
        const auto fields = SplitCharSv(nb, ':');
        if (fields.size() < 6)
            continue;
        const std::string stepType  = ToLowerAscii(TrimSv(fields[0]));
        if (stepType == "lights-cabinet")
            continue;
        if (hasDanceSingle && stepType != "dance-single")
            continue;

        const std::string descAuthor = TrimSv(fields[1]);
        if (outMeta.charter == "Unknown" && !descAuthor.empty())
            outMeta.charter = descAuthor;

        std::string diffClass = TrimSv(fields[2]);
        if (diffClass.empty())
            diffClass = "Normal";
        double meterVal = 0.0;
        ParseDoubleSafe(fields[3], meterVal);

        // Rejoin fields[5..] in case note data had any stray colons
        std::string noteData(fields[5]);
        for (std::size_t fi = 6; fi < fields.size(); ++fi)
        {
            noteData.push_back(':');
            noteData.append(fields[fi]);
        }

        struct SmResolvedNote
        {
            int  timeMs     = 0;
            int  col        = 0;
            int  durationMs = 0;
            bool isDownbeat = false;
        };
        std::vector<SmResolvedNote> smNotes;
        std::array<int, 4> holdStartMs = { -1, -1, -1, -1 };

        const auto measures = SplitCharSv(noteData, ',');
        for (std::size_t mIdx = 0; mIdx < measures.size(); ++mIdx)
        {
            std::vector<std::string> rows;
            for (std::string_view rawRow : SplitLinesSv(measures[mIdx]))
            {
                std::string r = TrimSv(rawRow);
                if (r.size() >= 4)
                    rows.push_back(std::move(r));
            }
            if (rows.empty())
                continue;

            const std::size_t rowCount = rows.size();
            for (std::size_t rIdx = 0; rIdx < rowCount; ++rIdx)
            {
                const double beat  = static_cast<double>(mIdx) * 4.0 +
                                     4.0 * (static_cast<double>(rIdx) / static_cast<double>(rowCount));
                const int    rowMs = beatToMs(beat);
                const auto&  row   = rows[rIdx];
                const int    cols  = std::min(4, static_cast<int>(row.size()));

                for (int c = 0; c < cols; ++c)
                {
                    const char ch = row[static_cast<std::size_t>(c)];
                    if (ch == '1') // Tap
                    {
                        if (holdStartMs[static_cast<std::size_t>(c)] >= 0)
                        {
                            const int hs = holdStartMs[static_cast<std::size_t>(c)];
                            smNotes.push_back({ hs, c, std::max(60, rowMs - hs), false });
                            holdStartMs[static_cast<std::size_t>(c)] = -1;
                        }
                        smNotes.push_back({ rowMs, c, 0, rIdx == 0 });
                    }
                    else if (ch == '2' || ch == '4') // Hold head / Roll head
                    {
                        if (holdStartMs[static_cast<std::size_t>(c)] >= 0)
                        {
                            const int hs = holdStartMs[static_cast<std::size_t>(c)];
                            smNotes.push_back({ hs, c, std::max(60, rowMs - hs), false });
                        }
                        holdStartMs[static_cast<std::size_t>(c)] = rowMs;
                    }
                    else if (ch == '3') // Hold / Roll tail
                    {
                        const int hs = holdStartMs[static_cast<std::size_t>(c)];
                        if (hs >= 0)
                        {
                            smNotes.push_back({ hs, c, std::max(60, rowMs - hs), (hs == beatToMs(std::floor(beat / 4.0) * 4.0)) });
                            holdStartMs[static_cast<std::size_t>(c)] = -1;
                        }
                    }
                    // '0', 'M' (mine), 'L', 'F' ignored
                }
            }
        }

        for (int c = 0; c < 4; ++c)
        {
            if (holdStartMs[static_cast<std::size_t>(c)] >= 0)
                smNotes.push_back({ holdStartMs[static_cast<std::size_t>(c)], c, 200, false });
        }

        if (smNotes.empty())
            continue;

        std::stable_sort(smNotes.begin(), smNotes.end(),
                         [](const SmResolvedNote& a, const SmResolvedNote& b) {
                             if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                             return a.col < b.col;
                         });

        ChartData data;
        data.version = 4;
        for (const auto& seg : bpmSegs)
        {
            data.timingPoints.push_back({ beatToMs(seg.beat), static_cast<float>(seg.bpm), 4, 4 });
        }

        DualZoneLaneRouter router;
        int lastMouseMs = -10000;
        for (std::size_t i = 0; i < smNotes.size(); ++i)
        {
            const auto& sn    = smNotes[i];
            const float normX = (static_cast<float>(sn.col) + 0.5f) / 4.0f;
            const float waveY = std::clamp(0.50f + 0.26f * std::sin(static_cast<float>(sn.timeMs) * 0.0020f +
                                                                    static_cast<float>(sn.col) * 0.9f),
                                           0.0f, 1.0f);

            const bool isJump = (i + 1 < smNotes.size() && std::abs(smNotes[i + 1].timeMs - sn.timeMs) <= 6);
            if (!router.TryPlaceSpecificLane(sn.timeMs, sn.durationMs, sn.col, data.keyboardNotes))
            {
                if (!router.TryPlaceKeyboard(sn.timeMs, sn.durationMs, normX, sn.isDownbeat && isJump, data.keyboardNotes))
                {
                    if (sn.timeMs - lastMouseMs > 15)
                    {
                        data.mouseNotes.push_back(MakeRawMouseCircle(sn.timeMs, normX, waveY));
                        lastMouseMs = sn.timeMs;
                    }
                }
            }

            // Musical downbeat / phrase right-hand mouse accompaniment
            if ((sn.isDownbeat || isJump || data.mouseNotes.empty()) && (sn.timeMs - lastMouseMs >= 380))
            {
                if (sn.durationMs >= 180)
                {
                    data.mouseNotes.push_back(MakeRawMouseSlider(
                        sn.timeMs, std::min(sn.durationMs, 950), normX, waveY, 1.0f - normX, 1.0f - waveY));
                }
                else
                {
                    data.mouseNotes.push_back(MakeRawMouseCircle(sn.timeMs, normX, waveY));
                }
                lastMouseMs = sn.timeMs;
            }
        }

        EnsureDualZoneBalance(data.keyboardNotes, data.mouseNotes);
        OptimizeMouseSpatialLayout(data.mouseNotes);
        BindCrossZoneLinks(data.keyboardNotes, data.mouseNotes);
        GenerateStageEventsForChart(data);

        if (!FinalizeAndValidateChart(data, outMeta.bpm))
            continue;

        DifficultyInfo di;
        di.name  = diffClass;
        di.level = (meterVal > 0.0)
                       ? static_cast<float>(std::clamp(meterVal, 1.0, 15.0))
                       : EstimateChartLevel(data, 0.0f);

        std::string baseStem = MakeSafeSlug(diffClass, "normal");
        std::string fileName = baseStem + ".json";
        int dupSuffix = 2;
        while (std::find(usedChartFiles.begin(), usedChartFiles.end(), fileName) != usedChartFiles.end())
        {
            fileName = baseStem + "_" + std::to_string(dupSuffix++) + ".json";
        }
        usedChartFiles.push_back(fileName);
        di.chartFile = fileName;
        PopulateDifficultyCounts(di, data);

        outDifficulties.emplace_back(std::move(di), std::move(data));
    }

    if (outDifficulties.empty())
        return false;

    std::stable_sort(outDifficulties.begin(), outDifficulties.end(),
                     [](const auto& a, const auto& b) { return a.first.level < b.first.level; });

    outMeta.difficulties.clear();
    for (const auto& entry : outDifficulties)
        outMeta.difficulties.push_back(entry.first);

    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. DetectFormat & ImportExternalPackage
// ═════════════════════════════════════════════════════════════════════════════

namespace
{

ExternalChartFormat ClassifyJsonFileFormat(const fs::path& jsonPath)
{
    std::string raw;
    if (!ReadWholeFileBinary(jsonPath, raw, 32ULL * 1024ULL * 1024ULL))
        return ExternalChartFormat::Unknown;
    try
    {
        const json j = json::parse(StripUtf8Bom(raw));
        if (!j.is_object())
            return ExternalChartFormat::Unknown;
        if (j.contains("BPMList") || j.contains("META") || j.contains("judgeLineList") || j.contains("formatVersion"))
            return ExternalChartFormat::PhiraJson;
        if (j.contains("difficulties") || j.contains("keyboard_notes") || j.contains("mouse_notes"))
            return ExternalChartFormat::SakuraNative;
    }
    catch (...)
    {
    }
    return ExternalChartFormat::Unknown;
}

ExternalChartFormat InspectDirectoryFormat(const fs::path& dirPath)
{
    std::error_code ec;
    if (fs::is_regular_file(dirPath / "info.json", ec) && !ec)
    {
        ChartLoader loader;
        if (loader.LoadChartInfo((dirPath / "info.json").string()).has_value())
            return ExternalChartFormat::SakuraNative;
    }

    bool sawPhiraMeta = false;
    bool sawOsu       = false;
    bool sawSm        = false;
    bool sawPhiraJson = false;
    bool sawSakura    = false;

    fs::recursive_directory_iterator it(dirPath, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end && !ec)
    {
        if (it.depth() > 3)
            it.disable_recursion_pending();
        if (it->is_regular_file(ec) && !ec)
        {
            const std::string fnameLower = ToLowerAscii(PathToUtf8(it->path().filename()));
            const std::string extLower   = PathExtLower(it->path());
            if (fnameLower == "info.yml" || fnameLower == "info.yaml" ||
                fnameLower == "info.txt" || fnameLower == "info.csv")
            {
                sawPhiraMeta = true;
            }
            else if (fnameLower == "info.json")
            {
                ChartLoader loader;
                if (loader.LoadChartInfo(it->path().string()).has_value())
                    sawSakura = true;
            }
            else if (extLower == ".osu")
            {
                sawOsu = true;
            }
            else if (extLower == ".sm")
            {
                sawSm = true;
            }
            else if (extLower == ".json")
            {
                if (ClassifyJsonFileFormat(it->path()) == ExternalChartFormat::PhiraJson)
                    sawPhiraJson = true;
            }
        }
        it.increment(ec);
    }

    if (sawSakura)    return ExternalChartFormat::SakuraNative;
    if (sawPhiraMeta) return ExternalChartFormat::PhiraPez;
    if (sawOsu)       return ExternalChartFormat::OsuArchive;
    if (sawSm)        return ExternalChartFormat::StepManiaSm;
    if (sawPhiraJson) return ExternalChartFormat::PhiraJson;
    return ExternalChartFormat::Unknown;
}

ExternalChartFormat PeekZipFormat(const fs::path& zipPath)
{
    std::string bytes;
    if (!ReadWholeFileBinary(zipPath, bytes, 128ULL * 1024ULL * 1024ULL) || bytes.size() < 22)
        return ExternalChartFormat::Unknown;
    // Scan local file headers (0x04034b50) for entry names
    bool hasInfoJson  = false;
    bool hasPhiraMeta = false;
    bool hasOsu       = false;
    bool hasSm        = false;
    bool hasAnyJson   = false;

    std::size_t pos = 0;
    while (pos + 30 <= bytes.size())
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data() + pos);
        const std::uint32_t sig = static_cast<std::uint32_t>(p[0]) |
                                  (static_cast<std::uint32_t>(p[1]) << 8U) |
                                  (static_cast<std::uint32_t>(p[2]) << 16U) |
                                  (static_cast<std::uint32_t>(p[3]) << 24U);
        if (sig != 0x04034b50U)
        {
            ++pos;
            continue;
        }
        const std::uint16_t nameLen  = static_cast<std::uint16_t>(p[26]) | (static_cast<std::uint16_t>(p[27]) << 8U);
        const std::uint16_t extraLen = static_cast<std::uint16_t>(p[28]) | (static_cast<std::uint16_t>(p[29]) << 8U);
        const std::uint32_t compSz   = static_cast<std::uint32_t>(p[18]) |
                                       (static_cast<std::uint32_t>(p[19]) << 8U) |
                                       (static_cast<std::uint32_t>(p[20]) << 16U) |
                                       (static_cast<std::uint32_t>(p[21]) << 24U);
        if (pos + 30 + nameLen > bytes.size())
            break;
        const std::string entryLower = ToLowerAscii(std::string_view(bytes.data() + pos + 30, nameLen));
        const fs::path ep(entryLower);
        const std::string base = ep.filename().generic_string();
        const std::string ext  = ep.extension().generic_string();
        if (base == "info.json")                                                 hasInfoJson  = true;
        if (base == "info.yml" || base == "info.yaml" || base == "info.txt" || base == "info.csv") hasPhiraMeta = true;
        if (ext == ".osu")                                                       hasOsu       = true;
        if (ext == ".sm")                                                        hasSm        = true;
        if (ext == ".json" && base != "info.json")                               hasAnyJson   = true;
        pos += 30 + static_cast<std::size_t>(nameLen) + static_cast<std::size_t>(extraLen) +
               (compSz > 0 ? static_cast<std::size_t>(compSz) : 0U);
    }
    if (hasInfoJson)  return ExternalChartFormat::SakuraNative;
    if (hasPhiraMeta) return ExternalChartFormat::PhiraPez;
    if (hasOsu)       return ExternalChartFormat::OsuArchive;
    if (hasSm)        return ExternalChartFormat::StepManiaSm;
    if (hasAnyJson)   return ExternalChartFormat::PhiraPez;
    return ExternalChartFormat::Unknown;
}

struct PhiraPackageEntry
{
    std::string id;
    std::string title;
    std::string artist;
    std::string charter;
    std::string chartFile;
    std::string musicFile;
    std::string coverFile;
    std::string levelText;
    float       numericLevel = 0.0f;
    int         previewMs    = 0;
};

void ParsePhiraInfoYml(std::string_view text, std::vector<PhiraPackageEntry>& outEntries)
{
    PhiraPackageEntry entry;
    bool anyField = false;

    for (std::string_view rawLine : SplitLinesSv(StripUtf8Bom(text)))
    {
        std::string line = TrimSv(rawLine);
        if (line.empty() || line.front() == '#')
            continue;
        if (line.rfind("- ", 0) == 0)
            line = TrimSv(std::string_view(line).substr(2));

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = ToLowerAscii(TrimSv(std::string_view(line).substr(0, colon)));
        std::string val       = TrimSv(std::string_view(line).substr(colon + 1));
        // Strip trailing YAML inline comment when outside quotes
        if (!val.empty() && val.front() != '"' && val.front() != '\'')
        {
            const std::size_t hashComment = val.find(" #");
            if (hashComment != std::string::npos)
                val = TrimSv(std::string_view(val).substr(0, hashComment));
        }
        val = StripQuotes(val);
        if (val.empty())
            continue;

        if (key == "id")                   { entry.id        = val; anyField = true; }
        else if (key == "name" || key == "title") { entry.title = val; anyField = true; }
        else if (key == "composer" || key == "artist") { entry.artist = val; anyField = true; }
        else if (key == "charter" || key == "designer") { entry.charter = val; anyField = true; }
        else if (key == "chart")           { entry.chartFile = NormalizeRelAssetName(val); anyField = true; }
        else if (key == "music" || key == "song") { entry.musicFile = NormalizeRelAssetName(val); anyField = true; }
        else if (key == "illustration" || key == "picture" || key == "image" || key == "cover")
        {
            entry.coverFile = NormalizeRelAssetName(val);
            anyField = true;
        }
        else if (key == "level")           { entry.levelText = val; anyField = true; }
        else if (key == "difficulty")
        {
            double d = 0.0;
            if (ParseDoubleSafe(val, d) && d > 0.0)
                entry.numericLevel = static_cast<float>(std::clamp(d, 1.0, 15.0));
            else if (entry.levelText.empty())
                entry.levelText = val;
            anyField = true;
        }
        else if (key == "previewstart" || key == "preview_start" || key == "previewtime")
        {
            double p = 0.0;
            if (ParseDoubleSafe(val, p) && p > 0.0)
                entry.previewMs = static_cast<int>(std::llround((p < 600.0) ? (p * 1000.0) : p));
        }
    }
    if (anyField)
        outEntries.push_back(std::move(entry));
}

void ParsePhiraInfoTxt(std::string_view text, std::vector<PhiraPackageEntry>& outEntries)
{
    PhiraPackageEntry cur;
    bool hasCur = false;

    for (std::string_view rawLine : SplitLinesSv(StripUtf8Bom(text)))
    {
        const std::string line = TrimSv(rawLine);
        if (line.empty())
            continue;
        if (line == "#")
        {
            if (hasCur)
            {
                outEntries.push_back(std::move(cur));
                cur    = PhiraPackageEntry{};
                hasCur = false;
            }
            continue;
        }
        if (line.front() == '#')
            continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = ToLowerAscii(TrimSv(std::string_view(line).substr(0, colon)));
        const std::string val = StripQuotes(std::string_view(line).substr(colon + 1));
        if (val.empty())
            continue;

        if (key == "name" || key == "title")        { cur.title     = val; hasCur = true; }
        else if (key == "composer" || key == "artist") { cur.artist = val; hasCur = true; }
        else if (key == "charter" || key == "designer") { cur.charter = val; hasCur = true; }
        else if (key == "song" || key == "music")   { cur.musicFile = NormalizeRelAssetName(val); hasCur = true; }
        else if (key == "picture" || key == "illustration" || key == "image")
        {
            cur.coverFile = NormalizeRelAssetName(val);
            hasCur = true;
        }
        else if (key == "chart")                    { cur.chartFile = NormalizeRelAssetName(val); hasCur = true; }
        else if (key == "level")                    { cur.levelText = val; hasCur = true; }
    }
    if (hasCur)
        outEntries.push_back(std::move(cur));
}

std::vector<std::string> ParseCsvRow(std::string_view line)
{
    std::vector<std::string> cols;
    std::string cur;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            if (inQuotes && (i + 1) < line.size() && line[i + 1] == '"')
            {
                cur.push_back('"');
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
        }
        else if (c == ',' && !inQuotes)
        {
            cols.push_back(TrimSv(cur));
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    cols.push_back(TrimSv(cur));
    return cols;
}

void ParsePhiraInfoCsv(std::string_view text, std::vector<PhiraPackageEntry>& outEntries)
{
    const auto lines = SplitLinesSv(StripUtf8Bom(text));
    int colChart = 0, colMusic = 1, colImage = 2, colName = 6, colLevel = 7, colArtist = -1, colCharter = 9;
    bool parsedHeader = false;

    for (std::string_view rawLine : lines)
    {
        const std::string trimmed = TrimSv(rawLine);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        const auto cols = ParseCsvRow(trimmed);
        if (cols.size() < 2)
            continue;

        if (!parsedHeader)
        {
            const std::string c0 = ToLowerAscii(cols[0]);
            if (c0 == "chart" || c0 == "chartfile")
            {
                colChart = -1; colMusic = -1; colImage = -1;
                colName  = -1; colLevel = -1; colArtist = -1; colCharter = -1;
                for (int ci = 0; ci < static_cast<int>(cols.size()); ++ci)
                {
                    const std::string h = ToLowerAscii(cols[static_cast<std::size_t>(ci)]);
                    if (h == "chart")                                colChart   = ci;
                    else if (h == "music" || h == "song")            colMusic   = ci;
                    else if (h == "image" || h == "picture" || h == "illustration") colImage = ci;
                    else if (h == "name" || h == "title")            colName    = ci;
                    else if (h == "difficulty" || h == "level")      colLevel   = ci;
                    else if (h == "composer" || h == "artist")       colArtist  = ci;
                    else if (h == "designer" || h == "charter")      colCharter = ci;
                }
                parsedHeader = true;
                continue;
            }
            parsedHeader = true;
        }

        auto getCol = [&](int idx) -> std::string {
            if (idx < 0 || idx >= static_cast<int>(cols.size()))
                return "";
            return StripQuotes(cols[static_cast<std::size_t>(idx)]);
        };

        PhiraPackageEntry e;
        e.chartFile = NormalizeRelAssetName(getCol(colChart));
        e.musicFile = NormalizeRelAssetName(getCol(colMusic));
        e.coverFile = NormalizeRelAssetName(getCol(colImage));
        e.title     = getCol(colName);
        e.levelText = getCol(colLevel);
        e.artist    = getCol(colArtist);
        e.charter   = getCol(colCharter);
        if (!e.chartFile.empty() || !e.title.empty())
            outEntries.push_back(std::move(e));
    }
}

fs::path UnwrapSingleSubdirectory(fs::path root)
{
    for (int depth = 0; depth < 4; ++depth)
    {
        std::error_code ec;
        int fileCount = 0;
        int dirCount  = 0;
        fs::path singleSub;
        for (const auto& entry : fs::directory_iterator(root, ec))
        {
            if (ec) break;
            if (entry.is_regular_file(ec))
                ++fileCount;
            else if (entry.is_directory(ec))
            {
                ++dirCount;
                singleSub = entry.path();
            }
        }
        if (fileCount == 0 && dirCount == 1)
            root = singleSub;
        else
            break;
    }
    return root;
}

std::optional<fs::path> FindAssetFile(const fs::path& workDir,
                                      std::string_view preferredRel,
                                      const std::vector<std::string_view>& allowedExts)
{
    std::error_code ec;
    if (!preferredRel.empty())
    {
        const std::string norm = NormalizeRelAssetName(preferredRel);
        const fs::path direct  = workDir / fs::path(norm);
        if (fs::is_regular_file(direct, ec) && !ec)
            return direct;

        const std::string wantNameLower = ToLowerAscii(fs::path(norm).filename().generic_string());
        fs::recursive_directory_iterator it(workDir, fs::directory_options::skip_permission_denied, ec), end;
        while (it != end && !ec)
        {
            if (it.depth() > 3) it.disable_recursion_pending();
            if (it->is_regular_file(ec) && !ec)
            {
                if (ToLowerAscii(PathToUtf8(it->path().filename())) == wantNameLower)
                    return it->path();
            }
            it.increment(ec);
        }
    }

    std::optional<fs::path> largestMatch;
    std::uintmax_t          bestSize = 0;
    ec.clear();
    fs::recursive_directory_iterator it(workDir, fs::directory_options::skip_permission_denied, ec), end;
    while (it != end && !ec)
    {
        if (it.depth() > 3) it.disable_recursion_pending();
        if (it->is_regular_file(ec) && !ec)
        {
            const std::string ext = PathExtLower(it->path());
            for (std::string_view allowed : allowedExts)
            {
                if (ext == allowed)
                {
                    const auto sz = fs::file_size(it->path(), ec);
                    if (!ec && (!largestMatch.has_value() || sz > bestSize))
                    {
                        bestSize     = sz;
                        largestMatch = it->path();
                    }
                }
            }
        }
        it.increment(ec);
    }
    return largestMatch;
}

std::string CopyAssetIntoOutDir(const std::optional<fs::path>& srcFile,
                                const fs::path& outDir,
                                std::string_view fallbackBaseName,
                                std::string_view defaultIfMissing)
{
    if (!srcFile.has_value())
        return std::string(defaultIfMissing);

    std::error_code ec;
    const std::string ext      = PathExtLower(*srcFile);
    const std::string safeStem = MakeSafeSlug(PathToUtf8(srcFile->stem()), fallbackBaseName);
    const std::string destName = safeStem + (ext.empty() ? std::string("") : ext);
    const fs::path    destPath = outDir / destName;
    fs::create_directories(outDir, ec);
    fs::copy_file(*srcFile, destPath, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return std::string(defaultIfMissing);
    return destName;
}

} // namespace

ExternalChartFormat ChartConverter::DetectFormat(const std::filesystem::path& sourcePath)
{
    std::error_code ec;
    if (fs::is_directory(sourcePath, ec) && !ec)
        return InspectDirectoryFormat(sourcePath);

    const std::string ext = PathExtLower(sourcePath);
    if (ext == ".pez")    return ExternalChartFormat::PhiraPez;
    if (ext == ".osz")    return ExternalChartFormat::OsuArchive;
    if (ext == ".sm")     return ExternalChartFormat::StepManiaSm;
    if (ext == ".osu")    return ExternalChartFormat::OsuFile;
    if (ext == ".sakura") return ExternalChartFormat::SakuraNative;

    if (ext == ".json")
    {
        if (fs::is_regular_file(sourcePath, ec) && !ec)
            return ClassifyJsonFileFormat(sourcePath);
        if (ToLowerAscii(PathToUtf8(sourcePath.filename())) == "info.json")
            return ExternalChartFormat::SakuraNative;
        return ExternalChartFormat::Unknown;
    }

    if (ext == ".zip" && fs::is_regular_file(sourcePath, ec) && !ec)
        return PeekZipFormat(sourcePath);

    return ExternalChartFormat::Unknown;
}

ConversionResult ChartConverter::ImportExternalPackage(const std::filesystem::path& sourcePath,
                                                       const std::filesystem::path& destChartsRoot)
{
    ConversionResult result;
    result.format = DetectFormat(sourcePath);

    std::error_code ec;
    if (!fs::exists(sourcePath, ec) || ec)
    {
        result.error = "未找到待转换的谱面路径";
        return result;
    }
    if (destChartsRoot.empty())
    {
        result.error = "目标曲库目录为空";
        return result;
    }

    fs::create_directories(destChartsRoot, ec);
    if (ec)
    {
        result.error = "无法创建目标曲库目录: " + ec.message();
        return result;
    }

    const auto nowTicks = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string stagingTag = sakura::utils::ComputeSha256Hex(
        PathToUtf8(sourcePath) + "_" + std::to_string(nowTicks)).substr(0, 12);
    const fs::path stagingRoot = destChartsRoot / (".sakura_import_staging_" + stagingTag);
    const fs::path extractDir  = stagingRoot / "unpacked";
    const fs::path outBuildDir = stagingRoot / "built";

    struct StagingGuard
    {
        fs::path path;
        ~StagingGuard()
        {
            std::error_code rmEc;
            if (!path.empty())
                fs::remove_all(path, rmEc);
        }
    } guard{ stagingRoot };

    fs::create_directories(outBuildDir, ec);
    if (ec)
    {
        result.error = "无法创建临时转换目录";
        return result;
    }

    fs::path workDir;
    std::optional<fs::path> singleSourceFile;
    const bool isDir = fs::is_directory(sourcePath, ec) && !ec;
    const std::string srcExt = PathExtLower(sourcePath);

    if (isDir)
    {
        workDir = UnwrapSingleSubdirectory(sourcePath);
    }
    else if (srcExt == ".pez" || srcExt == ".osz" || srcExt == ".zip")
    {
        fs::create_directories(extractDir, ec);
        if (!sakura::utils::ExtractZipArchive(sourcePath, extractDir))
        {
            result.error = "外部谱面归档解压失败（请确认归档未损坏）";
            return result;
        }
        workDir = UnwrapSingleSubdirectory(extractDir);
        if (result.format == ExternalChartFormat::Unknown || result.format == ExternalChartFormat::SakuraNative)
            result.format = InspectDirectoryFormat(workDir);
    }
    else if (fs::is_regular_file(sourcePath, ec) && !ec)
    {
        singleSourceFile = sourcePath;
        workDir          = sourcePath.has_parent_path() ? sourcePath.parent_path() : destChartsRoot;
    }
    else
    {
        result.error = "不支持的外部谱面输入类型";
        return result;
    }

    if (result.format == ExternalChartFormat::Unknown)
    {
        result.format = singleSourceFile.has_value()
                            ? DetectFormat(*singleSourceFile)
                            : InspectDirectoryFormat(workDir);
    }

    ChartInfo chartInfo;
    chartInfo.version = 4;
    std::vector<std::pair<DifficultyInfo, ChartData>> convertedDiffs;

    // ── Branch A: osu! (.osz / .osu) ────────────────────────────────────────
    if (result.format == ExternalChartFormat::OsuArchive || result.format == ExternalChartFormat::OsuFile)
    {
        std::vector<fs::path> osuFiles;
        if (singleSourceFile.has_value())
        {
            osuFiles.push_back(*singleSourceFile);
        }
        else
        {
            fs::recursive_directory_iterator it(workDir, fs::directory_options::skip_permission_denied, ec), end;
            while (it != end && !ec)
            {
                if (it.depth() > 3) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && !ec && PathExtLower(it->path()) == ".osu")
                    osuFiles.push_back(it->path());
                it.increment(ec);
            }
            std::sort(osuFiles.begin(), osuFiles.end());
        }

        bool gotMeta = false;
        for (const auto& osuPath : osuFiles)
        {
            std::string osuText;
            if (!ReadWholeFileBinary(osuPath, osuText))
                continue;
            ChartInfo      curMeta;
            DifficultyInfo curDiff;
            auto dataOpt = ConvertOsuBeatmapText(osuText, &curMeta, &curDiff);
            if (!dataOpt.has_value())
                continue;
            if (!gotMeta)
            {
                chartInfo = std::move(curMeta);
                gotMeta   = true;
            }
            else
            {
                if (chartInfo.musicFile.empty() && !curMeta.musicFile.empty())
                    chartInfo.musicFile = curMeta.musicFile;
                if (chartInfo.coverFile.empty() && !curMeta.coverFile.empty())
                {
                    chartInfo.coverFile      = curMeta.coverFile;
                    chartInfo.backgroundFile = curMeta.backgroundFile;
                }
            }
            convertedDiffs.emplace_back(std::move(curDiff), std::move(*dataOpt));
        }
    }
    // ── Branch B: StepMania (.sm) ───────────────────────────────────────────
    else if (result.format == ExternalChartFormat::StepManiaSm)
    {
        std::vector<fs::path> smFiles;
        if (singleSourceFile.has_value())
        {
            smFiles.push_back(*singleSourceFile);
        }
        else
        {
            fs::recursive_directory_iterator it(workDir, fs::directory_options::skip_permission_denied, ec), end;
            while (it != end && !ec)
            {
                if (it.depth() > 3) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && !ec && PathExtLower(it->path()) == ".sm")
                    smFiles.push_back(it->path());
                it.increment(ec);
            }
            std::sort(smFiles.begin(), smFiles.end());
        }

        for (const auto& smPath : smFiles)
        {
            std::string smText;
            if (!ReadWholeFileBinary(smPath, smText))
                continue;
            ChartInfo curMeta;
            std::vector<std::pair<DifficultyInfo, ChartData>> curDiffs;
            if (!ConvertStepManiaSmText(smText, curMeta, curDiffs))
                continue;
            if (convertedDiffs.empty())
                chartInfo = std::move(curMeta);
            for (auto& pair : curDiffs)
                convertedDiffs.push_back(std::move(pair));
        }
    }
    // ── Branch C: Phira (.pez / info.yml / info.txt / info.csv / Phigros & RPE .json) ──
    else
    {
        if (result.format == ExternalChartFormat::Unknown)
            result.format = singleSourceFile.has_value() ? ExternalChartFormat::PhiraJson : ExternalChartFormat::PhiraPez;

        std::vector<PhiraPackageEntry> pkgEntries;
        if (!singleSourceFile.has_value())
        {
            for (const char* ymlName : { "info.yml", "info.yaml" })
            {
                std::string txt;
                if (fs::is_regular_file(workDir / ymlName, ec) && !ec && ReadWholeFileBinary(workDir / ymlName, txt))
                    ParsePhiraInfoYml(txt, pkgEntries);
            }
            {
                std::string txt;
                if (fs::is_regular_file(workDir / "info.txt", ec) && !ec && ReadWholeFileBinary(workDir / "info.txt", txt))
                    ParsePhiraInfoTxt(txt, pkgEntries);
            }
            {
                std::string txt;
                if (fs::is_regular_file(workDir / "info.csv", ec) && !ec && ReadWholeFileBinary(workDir / "info.csv", txt))
                    ParsePhiraInfoCsv(txt, pkgEntries);
            }
        }

        std::vector<fs::path> jsonFiles;
        if (singleSourceFile.has_value())
        {
            jsonFiles.push_back(*singleSourceFile);
        }
        else
        {
            fs::recursive_directory_iterator it(workDir, fs::directory_options::skip_permission_denied, ec), end;
            while (it != end && !ec)
            {
                if (it.depth() > 3) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && !ec && PathExtLower(it->path()) == ".json")
                {
                    const std::string fnameLower = ToLowerAscii(PathToUtf8(it->path().filename()));
                    if (fnameLower != "info.json")
                        jsonFiles.push_back(it->path());
                }
                it.increment(ec);
            }
            std::sort(jsonFiles.begin(), jsonFiles.end());
        }

        for (const auto& jPath : jsonFiles)
        {
            std::string jsonText;
            if (!ReadWholeFileBinary(jPath, jsonText))
                continue;

            ChartInfo   rpeMeta;
            std::string rpeLevelText;
            auto dataOpt = ConvertAnyPhiraJson(jsonText, 120.0f, &rpeMeta, &rpeLevelText);
            if (!dataOpt.has_value())
                continue;

            // Match against package metadata entry by chart filename (or fallback to first entry)
            const std::string jFileLower = ToLowerAscii(PathToUtf8(jPath.filename()));
            const PhiraPackageEntry* matched = nullptr;
            for (const auto& pe : pkgEntries)
            {
                if (!pe.chartFile.empty() &&
                    ToLowerAscii(fs::path(pe.chartFile).filename().generic_string()) == jFileLower)
                {
                    matched = &pe;
                    break;
                }
            }
            if (matched == nullptr && pkgEntries.size() == 1 && jsonFiles.size() == 1)
                matched = &pkgEntries.front();
            else if (matched == nullptr && !pkgEntries.empty() && chartInfo.title.empty())
                matched = &pkgEntries.front();

            if (matched != nullptr)
            {
                if (chartInfo.id.empty() && !matched->id.empty())             chartInfo.id          = matched->id;
                if (chartInfo.title.empty() && !matched->title.empty())       chartInfo.title       = matched->title;
                if (chartInfo.artist.empty() && !matched->artist.empty())     chartInfo.artist      = matched->artist;
                if (chartInfo.charter.empty() && !matched->charter.empty())   chartInfo.charter     = matched->charter;
                if (chartInfo.musicFile.empty() && !matched->musicFile.empty()) chartInfo.musicFile = matched->musicFile;
                if (chartInfo.coverFile.empty() && !matched->coverFile.empty())
                {
                    chartInfo.coverFile      = matched->coverFile;
                    chartInfo.backgroundFile = matched->coverFile;
                }
                if (chartInfo.previewTime == 0 && matched->previewMs > 0)     chartInfo.previewTime = matched->previewMs;
            }

            if (chartInfo.id.empty() && !rpeMeta.id.empty())             chartInfo.id          = rpeMeta.id;
            if (chartInfo.title.empty() && !rpeMeta.title.empty())       chartInfo.title       = rpeMeta.title;
            if (chartInfo.artist.empty() && !rpeMeta.artist.empty())     chartInfo.artist      = rpeMeta.artist;
            if (chartInfo.charter.empty() && !rpeMeta.charter.empty())   chartInfo.charter     = rpeMeta.charter;
            if (chartInfo.musicFile.empty() && !rpeMeta.musicFile.empty()) chartInfo.musicFile = rpeMeta.musicFile;
            if (chartInfo.coverFile.empty() && !rpeMeta.coverFile.empty())
            {
                chartInfo.coverFile      = rpeMeta.coverFile;
                chartInfo.backgroundFile = rpeMeta.coverFile;
            }
            if (!dataOpt->timingPoints.empty())
                chartInfo.bpm = dataOpt->timingPoints.front().bpm;

            const std::string effLevelText = (matched != nullptr && !matched->levelText.empty() &&
                                              (matched->chartFile.empty() ||
                                               ToLowerAscii(fs::path(matched->chartFile).filename().generic_string()) == jFileLower ||
                                               jsonFiles.size() == 1))
                                                 ? matched->levelText
                                                 : rpeLevelText;
            const float hintNumeric = (matched != nullptr && matched->numericLevel > 0.0f && jsonFiles.size() == 1)
                                          ? matched->numericLevel
                                          : ParseLevelFromText(effLevelText, 0.0f);

            DifficultyInfo di;
            di.name      = ExtractDiffNameFromLevelText(effLevelText, PathToUtf8(jPath.stem()));
            di.level     = EstimateChartLevel(*dataOpt, hintNumeric);
            di.chartFile = MakeSafeSlug(di.name, "in") + ".json";
            PopulateDifficultyCounts(di, *dataOpt);
            convertedDiffs.emplace_back(std::move(di), std::move(*dataOpt));
        }
    }

    if (convertedDiffs.empty())
    {
        result.error = "未能在外部谱面包中解析出任何有效谱面难度";
        return result;
    }

    // Fill fallback metadata defaults
    const std::string stemFallback = PathToUtf8(sourcePath.stem());
    if (chartInfo.title.empty())   chartInfo.title   = !stemFallback.empty() ? stemFallback : "Imported Chart";
    if (chartInfo.artist.empty())  chartInfo.artist  = "Unknown";
    if (chartInfo.charter.empty()) chartInfo.charter = "Unknown";
    chartInfo.bpm     = ClampBpm(chartInfo.bpm, convertedDiffs.front().second.timingPoints.front().bpm);
    chartInfo.offset  = std::clamp(chartInfo.offset, -60000, 60000);
    chartInfo.version = 4;

    const std::string safeSlug = MakeSafeSlug(
        !chartInfo.id.empty() ? chartInfo.id : (!stemFallback.empty() ? stemFallback : chartInfo.title),
        "imported_chart");
    chartInfo.id = safeSlug;

    // Discover and copy audio, cover, background, and optional video assets
    const auto audioSrc = FindAssetFile(workDir, chartInfo.musicFile, { ".ogg", ".mp3", ".wav", ".flac" });
    const auto coverSrc = FindAssetFile(workDir, chartInfo.coverFile, { ".png", ".jpg", ".jpeg", ".webp" });
    const auto bgSrc    = (!chartInfo.backgroundFile.empty() && chartInfo.backgroundFile != chartInfo.coverFile)
                              ? FindAssetFile(workDir, chartInfo.backgroundFile, { ".png", ".jpg", ".jpeg", ".webp" })
                              : coverSrc;
    const auto videoSrc = !chartInfo.videoFile.empty()
                              ? FindAssetFile(workDir, chartInfo.videoFile, { ".mp4", ".webm" })
                              : std::nullopt;

    chartInfo.musicFile      = CopyAssetIntoOutDir(audioSrc, outBuildDir, "music", "music.ogg");
    chartInfo.coverFile      = CopyAssetIntoOutDir(coverSrc, outBuildDir, "cover", "cover.png");
    chartInfo.backgroundFile = (bgSrc.has_value() && coverSrc.has_value() && *bgSrc == *coverSrc)
                                   ? chartInfo.coverFile
                                   : CopyAssetIntoOutDir(bgSrc, outBuildDir, "bg", chartInfo.coverFile);
    chartInfo.videoFile      = videoSrc.has_value()
                                   ? CopyAssetIntoOutDir(videoSrc, outBuildDir, "video", "")
                                   : "";

    // Sort difficulties by level ascending and assign unique chart_file names
    std::stable_sort(convertedDiffs.begin(), convertedDiffs.end(),
                     [](const auto& a, const auto& b) { return a.first.level < b.first.level; });

    chartInfo.difficulties.clear();
    std::vector<std::string> assignedFiles;
    for (auto& [diffInfo, chartData] : convertedDiffs)
    {
        std::string baseStem = MakeSafeSlug(diffInfo.name, "normal");
        std::string fileName = baseStem + ".json";
        int suffix = 2;
        while (std::find(assignedFiles.begin(), assignedFiles.end(), fileName) != assignedFiles.end() ||
               fileName == "info.json")
        {
            fileName = baseStem + "_" + std::to_string(suffix++) + ".json";
        }
        assignedFiles.push_back(fileName);
        diffInfo.chartFile = fileName;
        PopulateDifficultyCounts(diffInfo, chartData);

        const json chartJson = SerializeChartDataJson(chartData);
        if (!sakura::utils::AtomicWrite(outBuildDir / fileName, chartJson.dump(2)))
        {
            result.error = "写入转换后的难度数据失败: " + fileName;
            return result;
        }
        chartInfo.difficulties.push_back(diffInfo);
    }

    // Write Sakura Chart v4 info.json
    json infoJson;
    infoJson["version"]         = 4;
    infoJson["id"]              = chartInfo.id;
    infoJson["title"]           = chartInfo.title;
    infoJson["artist"]          = chartInfo.artist;
    infoJson["charter"]         = chartInfo.charter;
    infoJson["source"]          = chartInfo.source;
    infoJson["tags"]            = chartInfo.tags;
    infoJson["music_file"]      = chartInfo.musicFile;
    infoJson["cover_file"]      = chartInfo.coverFile;
    infoJson["background_file"] = chartInfo.backgroundFile;
    if (!chartInfo.videoFile.empty())
        infoJson["video_file"]  = chartInfo.videoFile;
    infoJson["preview_time"]    = chartInfo.previewTime;
    infoJson["bpm"]             = chartInfo.bpm;
    infoJson["offset"]          = chartInfo.offset;

    json diffsArr = json::array();
    for (const auto& d : chartInfo.difficulties)
    {
        diffsArr.push_back({
            { "name", d.name },
            { "level", d.level },
            { "chart_file", d.chartFile },
            { "note_count", d.noteCount },
            { "hold_count", d.holdCount },
            { "mouse_note_count", d.mouseNoteCount },
            { "thumb_count", d.thumbCount },
            { "synergy_count", d.synergyCount }
        });
    }
    infoJson["difficulties"] = std::move(diffsArr);

    if (!sakura::utils::AtomicWrite(outBuildDir / "info.json", infoJson.dump(2)))
    {
        result.error = "写入 info.json 失败";
        return result;
    }

    // Verify output integrity via ChartLoader before promoting to destChartsRoot/<safeSlug>
    ChartLoader verifier;
    const auto stagedInfo = verifier.LoadChartInfo((outBuildDir / "info.json").string());
    if (!stagedInfo.has_value())
    {
        result.error = "转换生成的 info.json 未通过 ChartLoader 校验";
        return result;
    }
    for (const auto& d : stagedInfo->difficulties)
    {
        const auto stagedData = verifier.LoadChartData((outBuildDir / d.chartFile).string());
        if (!stagedData.has_value())
        {
            result.error = "转换生成的谱面数据未通过校验: " + d.chartFile;
            return result;
        }
    }

    const fs::path finalDir = destChartsRoot / safeSlug;
    fs::remove_all(finalDir, ec);
    ec.clear();
    fs::rename(outBuildDir, finalDir, ec);
    if (ec)
    {
        ec.clear();
        fs::create_directories(finalDir, ec);
        fs::copy(outBuildDir, finalDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            result.error = "移动转换结果至曲库目录失败: " + ec.message();
            return result;
        }
    }

    auto finalInfo = verifier.LoadChartInfo((finalDir / "info.json").string());
    if (!finalInfo.has_value())
    {
        result.error = "最终谱面目录读取校验失败";
        return result;
    }

    result.ok                    = true;
    result.outputChartDir        = finalInfo->folderPath;
    result.chartInfo             = std::move(*finalInfo);
    result.convertedDifficulties = static_cast<int>(result.chartInfo.difficulties.size());

    LOG_INFO("[ChartConverter] 外部谱面转换成功: '{}' -> {} ({} 难度)",
             result.chartInfo.title, result.outputChartDir, result.convertedDifficulties);
    return result;
}

} // namespace sakura::game
