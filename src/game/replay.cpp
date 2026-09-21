#include "replay.h"
#include "utils/file_io.h"

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace sakura::game
{
std::string Replay::Hash(const ChartData& chart, int offsetMs)
{
    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&hash](uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (value >> (i * 8)) & 255;
            hash *= 1099511628211ULL;
        }
    };
    mix(offsetMs);
    mix(static_cast<uint32_t>(chart.timingPoints.size()));
    for(const auto& point:chart.timingPoints){mix(point.time);mix(std::bit_cast<uint32_t>(point.bpm));mix(point.timeSigNumerator);mix(point.timeSigDenominator);}
    mix(static_cast<uint32_t>(chart.svPoints.size()));
    for(const auto& point:chart.svPoints){mix(point.time);mix(std::bit_cast<uint32_t>(point.speed));for(unsigned char c:point.easing)mix(c);mix(0);}
    mix(static_cast<uint32_t>(chart.keyboardNotes.size()));
    for (const auto& note : chart.keyboardNotes)
    {
        mix(note.time); mix(note.lane); mix(static_cast<uint32_t>(note.type)); mix(note.duration);
    }
    mix(static_cast<uint32_t>(chart.mouseNotes.size()));
    for (const auto& note : chart.mouseNotes)
    {
        mix(note.time); mix(std::bit_cast<uint32_t>(note.x)); mix(std::bit_cast<uint32_t>(note.y));
        mix(static_cast<uint32_t>(note.type)); mix(note.sliderDuration);
        mix(static_cast<uint32_t>(note.sliderPath.size()));
        for (const auto& [x, y] : note.sliderPath) { mix(std::bit_cast<uint32_t>(x)); mix(std::bit_cast<uint32_t>(y)); }
    }
    return std::to_string(hash);
}

bool Replay::Save(const std::string& path) const
{
    nlohmann::json data = {{"format", "sakura-replay"}, {"version", 1}, {"chart_id", chartId},
        {"chart_hash", chartHash}, {"difficulty", difficulty}, {"rate", rate}, {"start_ms", startMs},
        {"pointer_scale_x",pointerScaleX}, {"pointer_scale_y",pointerScaleY}};
    auto& events = data["inputs"] = nlohmann::json::array();
    for (const auto& input : inputs)
        events.push_back({input.time, static_cast<int>(input.kind), input.lane, input.x, input.y});
    return sakura::utils::AtomicWrite(path, data.dump());
}

std::optional<Replay> Replay::Load(const std::string& path)
{
    try
    {
        if (std::filesystem::file_size(path) > 128 * 1024 * 1024) return std::nullopt;
        std::ifstream file(path);
        const auto data = nlohmann::json::parse(file);
        if (data.at("format") != "sakura-replay" || data.at("version") != 1) return std::nullopt;
        Replay replay;
        replay.chartId = data.at("chart_id").get<std::string>();
        replay.chartHash = data.at("chart_hash").get<std::string>();
        replay.difficulty = data.at("difficulty").get<int>();
        replay.rate = data.at("rate").get<float>();
        replay.startMs = data.at("start_ms").get<int>();
        replay.pointerScaleX=data.value("pointer_scale_x",1.0f); replay.pointerScaleY=data.value("pointer_scale_y",1.0f);
        if(!std::isfinite(replay.pointerScaleX)||!std::isfinite(replay.pointerScaleY)||replay.pointerScaleX<0.1f||replay.pointerScaleX>10||replay.pointerScaleY<0.1f||replay.pointerScaleY>10) return std::nullopt;
        if (!std::isfinite(replay.rate) || replay.rate < 0.5f || replay.rate > 2.0f ||
            replay.difficulty < 0 || replay.startMs < 0) return std::nullopt;
        const auto& inputs = data.at("inputs");
        if (!inputs.is_array() || inputs.size() > 2000000) return std::nullopt;
        int previous = -10000;
        replay.inputs.reserve(inputs.size());
        for (const auto& event : inputs)
        {
            if (!event.is_array() || event.size() != 5) return std::nullopt;
            SessionInput input;
            input.time = event[0].get<int>();
            const int kind = event[1].get<int>();
            input.lane = event[2].get<int>();
            input.x = event[3].get<float>();
            input.y = event[4].get<float>();
            if (kind < 0 || kind > static_cast<int>(InputKind::PointerScale) || input.time < previous ||
                input.time > 3600000 || input.lane < 0 || input.lane >= 4 ||
                !std::isfinite(input.x) || !std::isfinite(input.y) ||
                std::abs(input.x) > 10 || std::abs(input.y) > 10) return std::nullopt;
            input.kind = static_cast<InputKind>(kind);
            if(input.kind==InputKind::PointerScale && (input.x<0.1f || input.y<0.1f))return std::nullopt;
            previous = input.time;
            replay.inputs.push_back(input);
        }
        return replay;
    }
    catch (const std::exception&) { return std::nullopt; }
}
}
