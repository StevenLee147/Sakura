// season.cpp — 樱华赛季排位系统跨平台确定性逻辑引擎实现 (v3.0.0)

#include "game/season.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace sakura::game
{

namespace
{
struct PoolCandidate
{
    const char* chartId;
    const char* chartTitle;
    const char* difficulty;
    float       level;
};

constexpr std::array<PoolCandidate, 6> kVanguardPool{{
    { "petal_storm_vip",   "Petal Storm: Overdrive [VIP]", "Normal", 6.5f },
    { "sakura_sprint_vip", "樱语疾行 · 极速",               "Normal", 6.5f },
    { "sakura_breath",     "樱落呼吸",                     "Hard",   7.5f },
    { "instant_bloom_vip", "一瞬花绽 · 里绽",               "Hard",   8.5f },
    { "sakura_breath_vip", "樱花落下的呼吸 · 终章",         "Hard",   9.0f },
    { "distant_star_vip",  "遥远的星 · 回响",               "Hard",   9.5f },
}};

constexpr std::array<PoolCandidate, 6> kMidPool{{
    { "sakura_sprint_vip",         "樱语疾行 · 极速",               "Hard",   10.0f },
    { "petal_storm_vip",           "Petal Storm: Overdrive [VIP]", "Hard",   10.5f },
    { "shattered_threshold_omega", "破碎阈值 · Ω",                 "Hard",   11.0f },
    { "sidechain_dream",           "樱落侧链梦",                   "Expert", 11.5f },
    { "instant_bloom_vip",         "一瞬花绽 · 里绽",               "Expert", 12.0f },
    { "sakura_breath_vip",         "樱花落下的呼吸 · 终章",         "Expert", 12.5f },
}};

constexpr std::array<PoolCandidate, 6> kBossPool{{
    { "sakura_sprint",             "樱语疾行",                     "Expert", 13.0f },
    { "sakura_sprint_vip",         "樱语疾行 · 极速",               "Expert", 13.5f },
    { "shattered_threshold_omega", "破碎阈值 · Ω",                 "Expert", 13.8f },
    { "petal_storm_vip",           "Petal Storm: Overdrive [VIP]", "Expert", 14.5f },
    { "petal_storm_overdrive",     "Petal Storm: Overdrive",       "Expert", 14.5f },
    { "shattered_threshold_omega", "破碎阈值 · Ω",                 "Master", 15.0f },
}};

constexpr std::array<const char*, 6> kSeasonThemes{{
    "镜花幻月 · Mirror Blossom",
    "绯霞神乐 · Crimson Kagura",
    "星穹夜樱 · Astral Sakura",
    "流光剑心 · Luminous Blade",
    "霜月绝响 · Frostmoon Echo",
    "极星满开 · Zenith Bloom",
}};

std::uint32_t Fnv1a32(std::string_view text)
{
    std::uint32_t h = 2166136261u;
    for (unsigned char ch : text)
    {
        h ^= static_cast<std::uint32_t>(ch);
        h *= 16777619u;
    }
    return h;
}
} // namespace

std::string SeasonEngine::CurrentSeasonIdUtc8()
{
    using namespace std::chrono;
    const auto utc8Now = system_clock::now() + hours(8);
    const std::time_t raw = system_clock::to_time_t(utc8Now);
    std::tm tmUtc{};
#ifdef _WIN32
    gmtime_s(&tmUtc, &raw);
#else
    gmtime_r(&raw, &tmUtc);
#endif
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << (tmUtc.tm_year + 1900)
        << '-'
        << std::setw(2) << std::setfill('0') << (tmUtc.tm_mon + 1);
    return oss.str();
}

bool SeasonEngine::IsValidSeasonId(std::string_view s)
{
    if (s.size() != 7 || s[4] != '-')
        return false;
    for (std::size_t i : { std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5}, std::size_t{6} })
    {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    const int month = (s[5] - '0') * 10 + (s[6] - '0');
    return month >= 1 && month <= 12;
}

int SeasonEngine::MaxStageSPForSlot(int slotIndex)
{
    switch (slotIndex)
    {
    case 0: return 1000;
    case 1: return 1150;
    case 2: return 1350;
    default: return 0;
    }
}

SeasonSpec SeasonEngine::ComputeSeasonSpec(std::string_view seasonIdUtc8)
{
    const std::string resolved = IsValidSeasonId(seasonIdUtc8)
        ? std::string(seasonIdUtc8)
        : CurrentSeasonIdUtc8();

    const int year  = std::stoi(resolved.substr(0, 4));
    const int month = std::stoi(resolved.substr(5, 2));
    const int themeIdx = ((year * 12 + (month - 1)) % 6 + 6) % 6;

    SeasonSpec spec;
    spec.seasonId   = resolved;
    spec.themeTitle = kSeasonThemes[static_cast<std::size_t>(themeIdx)];

    auto pickFromPool = [&](const std::array<PoolCandidate, 6>& pool,
                            int slotIdx,
                            const char* role,
                            const char* roleCn) -> SeasonStageSpec
    {
        const std::uint32_t h = Fnv1a32(resolved + "#" + role);
        std::size_t idx = static_cast<std::size_t>(h % pool.size());
        for (std::size_t step = 0; step < pool.size(); ++step)
        {
            const std::size_t candIdx = (idx + step) % pool.size();
            bool used = false;
            for (int prev = 0; prev < slotIdx; ++prev)
            {
                if (spec.stages[static_cast<std::size_t>(prev)].chartId == pool[candIdx].chartId)
                {
                    used = true;
                    break;
                }
            }
            if (!used)
            {
                idx = candIdx;
                break;
            }
        }
        const auto& c = pool[idx];
        SeasonStageSpec st;
        st.slotIndex       = slotIdx;
        st.slotRole        = role;
        st.roleTitle       = roleCn;
        st.chartId         = c.chartId;
        st.chartTitle      = c.chartTitle;
        st.difficulty      = c.difficulty;
        st.difficultyLevel = c.level;
        st.maxStageSP      = MaxStageSPForSlot(slotIdx);
        return st;
    };

    spec.stages[0] = pickFromPool(kVanguardPool, 0, "vanguard", "先锋课题");
    spec.stages[1] = pickFromPool(kMidPool,      1, "mid",      "中坚课题");
    spec.stages[2] = pickFromPool(kBossPool,     2, "boss",     "大将课题");
    return spec;
}

int SeasonEngine::MatchSeasonSlot(const SeasonSpec& spec, std::string_view chartId, std::string_view difficulty)
{
    for (int i = 0; i < kStageCount; ++i)
    {
        const auto& st = spec.stages[static_cast<std::size_t>(i)];
        if (st.chartId == chartId && st.difficulty == difficulty)
            return i;
    }
    return -1;
}

int SeasonEngine::CalculateStageSP(const GameResult& result,
                                   float chartLevel,
                                   int slotIndex,
                                   float playbackRate)
{
    if (result.assisted || std::abs(playbackRate - 1.0f) >= 0.01f)
        return 0;
    const int maxSP = MaxStageSPForSlot(slotIndex);
    if (maxSP <= 0 || result.score <= 0 || result.accuracy <= 0.0f)
        return 0;

    const double acc = std::clamp(static_cast<double>(result.accuracy), 0.0, 100.0);
    double accFactor = 0.0;
    if (acc < 70.0)
        accFactor = 0.25 * (acc / 70.0);
    else if (acc < 90.0)
        accFactor = 0.25 + 0.35 * ((acc - 70.0) / 20.0);
    else if (acc < 95.0)
        accFactor = 0.60 + 0.16 * ((acc - 90.0) / 5.0);
    else if (acc < 98.0)
        accFactor = 0.76 + 0.12 * ((acc - 95.0) / 3.0);
    else if (acc < 99.5)
        accFactor = 0.88 + 0.07 * ((acc - 98.0) / 1.5);
    else
        accFactor = 0.95 + 0.05 * ((acc - 99.5) / 0.5);

    const double scoreNorm  = std::clamp(static_cast<double>(result.score) / 1000000.0, 0.0, 1.0);
    const double slotMin    = (slotIndex == 0) ? 6.0 : ((slotIndex == 1) ? 9.5 : 12.5);
    const double slotSpan   = (slotIndex == 0) ? 3.5 : ((slotIndex == 1) ? 3.0 : 2.5);
    const double lvNorm     = std::clamp((static_cast<double>(chartLevel) - slotMin) / slotSpan, 0.0, 1.0);
    const double comboBonus = result.isAllPerfect ? 0.030 : (result.isFullCombo ? 0.015 : 0.0);
    const double synBonus   = std::min(0.010, std::max(0, result.synergyCount) * 0.0005);

    const double ratio = std::clamp(
        0.845 * accFactor + 0.125 * scoreNorm + comboBonus + 0.008 * lvNorm + synBonus,
        0.0,
        1.0);
    const int sp = static_cast<int>(std::lround(ratio * static_cast<double>(maxSP)));
    return std::clamp(sp, 0, maxSP);
}

SeasonTierInfo SeasonEngine::EvaluateTier(int totalSP)
{
    const int safeSP = std::max(0, totalSP);
    SeasonTierInfo info;
    if (safeSP < 1200)
    {
        info.tierIndex = 1; info.tierName = "青铜樱"; info.nextTierName = "白银月";
        info.minSP = 0; info.nextTierThreshold = 1200; info.spToNextTier = 1200 - safeSP;
        info.colorR = 214; info.colorG = 154; info.colorB = 126;
    }
    else if (safeSP < 1900)
    {
        info.tierIndex = 2; info.tierName = "白银月"; info.nextTierName = "黄金华";
        info.minSP = 1200; info.nextTierThreshold = 1900; info.spToNextTier = 1900 - safeSP;
        info.colorR = 184; info.colorG = 212; info.colorB = 242;
    }
    else if (safeSP < 2500)
    {
        info.tierIndex = 3; info.tierName = "黄金华"; info.nextTierName = "绯红霞";
        info.minSP = 1900; info.nextTierThreshold = 2500; info.spToNextTier = 2500 - safeSP;
        info.colorR = 255; info.colorG = 216; info.colorB = 122;
    }
    else if (safeSP < 3050)
    {
        info.tierIndex = 4; info.tierName = "绯红霞"; info.nextTierName = "极星神乐";
        info.minSP = 2500; info.nextTierThreshold = 3050; info.spToNextTier = 3050 - safeSP;
        info.colorR = 255; info.colorG = 110; info.colorB = 156;
    }
    else
    {
        info.tierIndex = 5; info.tierName = "极星神乐"; info.nextTierName = "";
        info.minSP = 3050; info.nextTierThreshold = kMaxTotalSP; info.spToNextTier = 0;
        info.colorR = 158; info.colorG = 240; info.colorB = 255;
    }
    return info;
}

SeasonStanding SeasonEngine::ComputeLocalStanding(const SeasonSpec& spec,
                                                  const std::vector<GameResult>& bestScores)
{
    SeasonStanding st;
    st.seasonId = spec.seasonId;
    for (const auto& r : bestScores)
    {
        if (r.assisted)
            continue;
        const int slot = MatchSeasonSlot(spec, r.chartId, r.difficulty);
        if (slot < 0)
            continue;
        const int sp = CalculateStageSP(r, spec.stages[static_cast<std::size_t>(slot)].difficultyLevel, slot, 1.0f);
        const auto idx = static_cast<std::size_t>(slot);
        if (sp > st.stageSP[idx] || (sp == st.stageSP[idx] && r.score > st.stageScores[idx]))
        {
            st.stageSP[idx]         = sp;
            st.stageScores[idx]     = r.score;
            st.stageAccuracies[idx] = r.accuracy;
        }
    }
    st.totalSP = st.stageSP[0] + st.stageSP[1] + st.stageSP[2];
    st.tier    = EvaluateTier(st.totalSP);
    return st;
}

} // namespace sakura::game
