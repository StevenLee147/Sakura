#pragma once

#include <filesystem>
#include <string_view>

namespace sakura::utils
{
// Write beside the destination, flush, then atomically replace it. A failed write
// must never destroy the player's previous settings, chart, or replay.
bool AtomicWrite(const std::filesystem::path& path, std::string_view contents);
}
