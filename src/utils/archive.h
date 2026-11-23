#pragma once

// archive.h — Standard PKZIP (.sakura / .zip) package reader & writer

#include <cstdint>
#include <filesystem>
#include <string>

namespace sakura::utils
{

// Packs all regular files inside sourceDir into a standard PKZIP archive (.sakura / .zip).
bool CreateZipArchive(const std::filesystem::path& sourceDir,
                      const std::filesystem::path& archivePath);

// Extracts a standard PKZIP archive (.sakura / .zip, Store method) into destDir safely
// (rejects path traversal, absolute paths, symlinks, or archives over maxTotalBytes).
bool ExtractZipArchive(const std::filesystem::path& archivePath,
                       const std::filesystem::path& destDir,
                       std::uintmax_t maxTotalBytes = 128ULL * 1024ULL * 1024ULL);

// Computes IEEE 802.3 CRC-32.
std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t length);

} // namespace sakura::utils
