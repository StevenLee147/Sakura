// tests/test_v170_converter_and_vol4.cpp — v1.7.0 Deflate ZIP、Phira/osu!/SM 双区谱面转换器与内置纯原声曲库审计测试

#include "test_framework.h"

#include "game/chart_converter.h"
#include "game/chart_loader.h"
#include "utils/archive.h"
#include "utils/file_io.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace sakura::game;
using sakura::tests::Matchers::WithinAbs;

namespace
{

struct ScopedV170TempEnv
{
    fs::path dir;

    ScopedV170TempEnv()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::path(SAKURA_SOURCE_DIR) / "build" / "qa" /
              ("test_v170_" + std::to_string(stamp));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }

    ~ScopedV170TempEnv()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

// RFC 1951 Section 3.1.1 bit writer for deterministic raw-deflate test fixtures
struct DeflateBitWriter
{
    std::vector<std::uint8_t> bytes;
    std::uint8_t              cur    = 0;
    int                       bitPos = 0;

    void WriteBits(std::uint32_t value, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            if (((value >> i) & 1U) != 0U)
                cur = static_cast<std::uint8_t>(cur | static_cast<std::uint8_t>(1U << bitPos));
            if (++bitPos == 8)
            {
                bytes.push_back(cur);
                cur    = 0;
                bitPos = 0;
            }
        }
    }

    void WriteHuffmanMsb(std::uint32_t code, int len)
    {
        for (int i = len - 1; i >= 0; --i)
            WriteBits((code >> i) & 1U, 1);
    }

    void AlignToByte()
    {
        if (bitPos != 0)
        {
            bytes.push_back(cur);
            cur    = 0;
            bitPos = 0;
        }
    }

    void WriteFixedLiteral(std::uint8_t lit)
    {
        if (lit <= 143U)
            WriteHuffmanMsb(0x30U + static_cast<std::uint32_t>(lit), 8);
        else
            WriteHuffmanMsb(0x190U + static_cast<std::uint32_t>(lit - 144U), 9);
    }

    void WriteEobFixed()
    {
        WriteHuffmanMsb(0x00U, 7);
    }
};

std::vector<std::uint8_t> BuildDeflateStoredBlock(std::string_view plain, bool bfinal = true)
{
    DeflateBitWriter bw;
    bw.WriteBits(bfinal ? 1U : 0U, 1);
    bw.WriteBits(0U, 2); // BTYPE = 00 (stored)
    bw.AlignToByte();

    const auto len  = static_cast<std::uint16_t>(plain.size());
    const auto nlen = static_cast<std::uint16_t>(~len);
    bw.bytes.push_back(static_cast<std::uint8_t>(len & 0xFFU));
    bw.bytes.push_back(static_cast<std::uint8_t>((len >> 8U) & 0xFFU));
    bw.bytes.push_back(static_cast<std::uint8_t>(nlen & 0xFFU));
    bw.bytes.push_back(static_cast<std::uint8_t>((nlen >> 8U) & 0xFFU));
    for (char ch : plain)
        bw.bytes.push_back(static_cast<std::uint8_t>(ch));
    return bw.bytes;
}

std::vector<std::uint8_t> BuildDeflateFixedLiterals(std::string_view plain, bool bfinal = true)
{
    DeflateBitWriter bw;
    bw.WriteBits(bfinal ? 1U : 0U, 1);
    bw.WriteBits(1U, 2); // BTYPE = 01 (fixed Huffman)
    for (char ch : plain)
        bw.WriteFixedLiteral(static_cast<std::uint8_t>(ch));
    bw.WriteEobFixed();
    bw.AlignToByte();
    return bw.bytes;
}

// Builds a Fixed-Huffman (BTYPE=01) stream exercising both multi-byte LZ77 phrase back-references
// (length=8, distance=8 with distance extra bit) and overlapping single-byte RLE copies (length=10, distance=1).
std::vector<std::uint8_t> BuildDeflateFixedLz77(std::string& outExpected)
{
    outExpected.clear();
    DeflateBitWriter bw;
    bw.WriteBits(1U, 1); // BFINAL = 1
    bw.WriteBits(1U, 2); // BTYPE  = 01

    const std::string seed = "SAKURA==";
    for (char ch : seed)
    {
        bw.WriteFixedLiteral(static_cast<std::uint8_t>(ch));
        outExpected.push_back(ch);
    }

    // Repeat "SAKURA==" 40 times: length=8 -> sym 262 (code 6, 7 bits, 0 extra), dist=8 -> code 5 (5 bits) + extra bit 1
    for (int i = 0; i < 40; ++i)
    {
        bw.WriteHuffmanMsb(6U, 7);
        bw.WriteHuffmanMsb(5U, 5);
        bw.WriteBits(1U, 1);
        outExpected.append(seed);
    }

    // Emit '!' then repeat 3x with overlapping RLE: length=10 -> sym 264 (code 8, 7 bits, 0 extra), dist=1 -> code 0 (5 bits)
    bw.WriteFixedLiteral(static_cast<std::uint8_t>('!'));
    outExpected.push_back('!');
    for (int i = 0; i < 3; ++i)
    {
        bw.WriteHuffmanMsb(8U, 7);
        bw.WriteHuffmanMsb(0U, 5);
        outExpected.append(10, '!');
    }

    bw.WriteEobFixed();
    bw.AlignToByte();
    return bw.bytes;
}

struct RawZipEntry
{
    std::string   name;
    std::uint16_t method           = 8;
    std::string   compressedData;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t crc32            = 0;
};

void AppendLe16(std::string& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFFU));
    out.push_back(static_cast<char>((v >> 8U) & 0xFFU));
}

void AppendLe32(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFFU));
    out.push_back(static_cast<char>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((v >> 24U) & 0xFFU));
}

RawZipEntry MakeDeflateZipEntry(std::string name, std::string_view plainContent)
{
    const auto deflated = BuildDeflateFixedLiterals(plainContent, true);
    RawZipEntry entry;
    entry.name             = std::move(name);
    entry.method           = 8;
    entry.compressedData.assign(reinterpret_cast<const char*>(deflated.data()), deflated.size());
    entry.uncompressedSize = static_cast<std::uint32_t>(plainContent.size());
    entry.crc32            = sakura::utils::ComputeCrc32(
        reinterpret_cast<const std::uint8_t*>(plainContent.data()), plainContent.size());
    return entry;
}

bool WriteCustomZipArchive(const fs::path& archivePath, const std::vector<RawZipEntry>& entries)
{
    std::string blob;
    std::vector<std::uint32_t> localOffsets;
    localOffsets.reserve(entries.size());

    for (const auto& e : entries)
    {
        localOffsets.push_back(static_cast<std::uint32_t>(blob.size()));
        const auto compSz = static_cast<std::uint32_t>(e.compressedData.size());
        const auto nameSz = static_cast<std::uint16_t>(e.name.size());

        AppendLe32(blob, 0x04034b50U);
        AppendLe16(blob, 20);
        AppendLe16(blob, 0x0800U);
        AppendLe16(blob, e.method);
        AppendLe16(blob, 0);
        AppendLe16(blob, 0);
        AppendLe32(blob, e.crc32);
        AppendLe32(blob, compSz);
        AppendLe32(blob, e.uncompressedSize);
        AppendLe16(blob, nameSz);
        AppendLe16(blob, 0);
        blob.append(e.name);
        blob.append(e.compressedData);
    }

    const auto cdStart = static_cast<std::uint32_t>(blob.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e     = entries[i];
        const auto compSz = static_cast<std::uint32_t>(e.compressedData.size());
        const auto nameSz = static_cast<std::uint16_t>(e.name.size());

        AppendLe32(blob, 0x02014b50U);
        AppendLe16(blob, 20);
        AppendLe16(blob, 20);
        AppendLe16(blob, 0x0800U);
        AppendLe16(blob, e.method);
        AppendLe16(blob, 0);
        AppendLe16(blob, 0);
        AppendLe32(blob, e.crc32);
        AppendLe32(blob, compSz);
        AppendLe32(blob, e.uncompressedSize);
        AppendLe16(blob, nameSz);
        AppendLe16(blob, 0);
        AppendLe16(blob, 0);
        AppendLe16(blob, 0);
        AppendLe16(blob, 0);
        AppendLe32(blob, 0);
        AppendLe32(blob, localOffsets[i]);
        blob.append(e.name);
    }
    const auto cdSize = static_cast<std::uint32_t>(blob.size()) - cdStart;

    AppendLe32(blob, 0x06054b50U);
    AppendLe16(blob, 0);
    AppendLe16(blob, 0);
    AppendLe16(blob, static_cast<std::uint16_t>(entries.size()));
    AppendLe16(blob, static_cast<std::uint16_t>(entries.size()));
    AppendLe32(blob, cdSize);
    AppendLe32(blob, cdStart);
    AppendLe16(blob, 0);

    std::error_code ec;
    if (archivePath.has_parent_path())
        fs::create_directories(archivePath.parent_path(), ec);
    return sakura::utils::AtomicWrite(archivePath, blob);
}

std::string ReadBinaryFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string MakeDummyOggPayload()
{
    std::string ogg = "OggS";
    ogg.resize(64, '\0');
    ogg[5] = 0x02;
    return ogg;
}

std::string MakeMinimalPngPayload()
{
    static constexpr std::array<std::uint8_t, 67> kPng1x1 = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41,
        0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
        0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xDD, 0x8D,
        0xB0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42
    };
    return std::string(reinterpret_cast<const char*>(kPng1x1.data()), kPng1x1.size());
}

bool HasTimestampNear(const std::vector<int>& times, int targetMs, int toleranceMs)
{
    return std::any_of(times.begin(), times.end(),
        [&](int t) { return std::abs(t - targetMs) <= toleranceMs; });
}

} // namespace

TEST_CASE("InflateRawDeflate and ExtractZipArchive support RFC 1951 method=8 Deflate and reject bad CRC or oversized output", "[v170][archive][deflate]")
{
    ScopedV170TempEnv env;

    const std::string expectedStored  = "Sakura v1.7.0 Stored Block (BTYPE=00) Exact Payload!\n";
    const std::string expectedHuffman = "Sakura-樱 Phira .pez & osu! .osz Fixed-Huffman RFC 1951 Payload 0123456789!";
    std::string       expectedLz77;

    const auto rawStored  = BuildDeflateStoredBlock(expectedStored, true);
    const auto rawHuffman = BuildDeflateFixedLiterals(expectedHuffman, true);
    const auto rawLz77    = BuildDeflateFixedLz77(expectedLz77);
    REQUIRE(rawLz77.size() < expectedLz77.size() / 3);

    // 1. Direct InflateRawDeflate verification across BTYPE=00, Fixed-Huffman literals, and LZ77 back-references
    {
        std::string out;
        REQUIRE(sakura::utils::InflateRawDeflate(rawStored.data(), rawStored.size(), out));
        REQUIRE(out == expectedStored);
    }
    {
        std::string out;
        REQUIRE(sakura::utils::InflateRawDeflate(rawHuffman.data(), rawHuffman.size(), out));
        REQUIRE(out == expectedHuffman);
    }
    {
        std::string out;
        REQUIRE(sakura::utils::InflateRawDeflate(rawLz77.data(), rawLz77.size(), out));
        REQUIRE(out == expectedLz77);
    }

    // Reject oversized output and malformed bitstreams in InflateRawDeflate
    {
        std::string cappedOut;
        REQUIRE(!sakura::utils::InflateRawDeflate(rawLz77.data(), rawLz77.size(), cappedOut, 64));

        const std::array<std::uint8_t, 2> invalidBlockType = { 0x07U, 0x00U }; // BTYPE = 11 (reserved)
        std::string badOut;
        REQUIRE(!sakura::utils::InflateRawDeflate(invalidBlockType.data(), invalidBlockType.size(), badOut));
    }

    // 2. Wrap all 3 raw-deflate streams inside a valid PKZIP archive (method = 8, exact uncompressed CRC-32)
    RawZipEntry eStored;
    eStored.name             = "stored_block.txt";
    eStored.method           = 8;
    eStored.compressedData.assign(reinterpret_cast<const char*>(rawStored.data()), rawStored.size());
    eStored.uncompressedSize = static_cast<std::uint32_t>(expectedStored.size());
    eStored.crc32            = sakura::utils::ComputeCrc32(
        reinterpret_cast<const std::uint8_t*>(expectedStored.data()), expectedStored.size());

    RawZipEntry eHuffman;
    eHuffman.name             = "fixed_huffman.txt";
    eHuffman.method           = 8;
    eHuffman.compressedData.assign(reinterpret_cast<const char*>(rawHuffman.data()), rawHuffman.size());
    eHuffman.uncompressedSize = static_cast<std::uint32_t>(expectedHuffman.size());
    eHuffman.crc32            = sakura::utils::ComputeCrc32(
        reinterpret_cast<const std::uint8_t*>(expectedHuffman.data()), expectedHuffman.size());

    RawZipEntry eLz77;
    eLz77.name             = "sub/lz77_repeat.txt";
    eLz77.method           = 8;
    eLz77.compressedData.assign(reinterpret_cast<const char*>(rawLz77.data()), rawLz77.size());
    eLz77.uncompressedSize = static_cast<std::uint32_t>(expectedLz77.size());
    eLz77.crc32            = sakura::utils::ComputeCrc32(
        reinterpret_cast<const std::uint8_t*>(expectedLz77.data()), expectedLz77.size());

    const fs::path validZip = env.dir / "valid_deflate.zip";
    const fs::path outDir   = env.dir / "extracted_ok";
    REQUIRE(WriteCustomZipArchive(validZip, { eStored, eHuffman, eLz77 }));
    REQUIRE(sakura::utils::ExtractZipArchive(validZip, outDir));

    REQUIRE(ReadBinaryFile(outDir / "stored_block.txt") == expectedStored);
    REQUIRE(ReadBinaryFile(outDir / "fixed_huffman.txt") == expectedHuffman);
    REQUIRE(ReadBinaryFile(outDir / "sub" / "lz77_repeat.txt") == expectedLz77);

    // 3. Corrupted CRC-32 on method=8 entry must be rejected
    RawZipEntry badCrcEntry = eHuffman;
    badCrcEntry.crc32 ^= 0xA55AA55AU;
    const fs::path badCrcZip = env.dir / "corrupted_crc_deflate.zip";
    REQUIRE(WriteCustomZipArchive(badCrcZip, { badCrcEntry }));
    REQUIRE(!sakura::utils::ExtractZipArchive(badCrcZip, env.dir / "extracted_bad_crc"));

    // 4. Oversized uncompressed output (zip-bomb protection where compressed file < limit < uncompressed output)
    const fs::path lzOnlyZip = env.dir / "lz77_ratio.zip";
    REQUIRE(WriteCustomZipArchive(lzOnlyZip, { eLz77 }));
    const auto archiveBytes = fs::file_size(lzOnlyZip);
    REQUIRE(archiveBytes < 256);
    REQUIRE(expectedLz77.size() > 256);
    REQUIRE(!sakura::utils::ExtractZipArchive(lzOnlyZip, env.dir / "extracted_oversize", 256));
}

TEST_CASE("ChartConverter ConvertPhigrosOfficialJson converts formatVersion 3 Tap/Drag/Hold/Flick into valid dual-zone Chart v4", "[v170][converter][phigros]")
{
    const char* officialV3Json = R"json({
        "formatVersion": 3,
        "offset": 0.0,
        "judgeLineList": [
            {
                "bpm": 150.0,
                "speedEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "value": 2.2 }
                ],
                "judgeLineMoveEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 0.5, "end": 0.5, "start2": 0.2, "end2": 0.2 }
                ],
                "judgeLineRotateEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 0.0, "end": 0.0 }
                ],
                "judgeLineDisappearEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 1.0, "end": 1.0 }
                ],
                "notesAbove": [
                    { "type": 1, "time": 32,  "positionX": -3.2, "holdTime": 0,  "speed": 1.0, "floorPosition": 0.88 },
                    { "type": 1, "time": 64,  "positionX": -1.1, "holdTime": 0,  "speed": 1.0, "floorPosition": 1.76 },
                    { "type": 3, "time": 96,  "positionX":  1.1, "holdTime": 40, "speed": 1.0, "floorPosition": 2.64 },
                    { "type": 4, "time": 160, "positionX":  3.0, "holdTime": 0,  "speed": 1.0, "floorPosition": 4.40 },
                    { "type": 2, "time": 192, "positionX": -2.0, "holdTime": 0,  "speed": 1.0, "floorPosition": 5.28 },
                    { "type": 2, "time": 208, "positionX": -0.5, "holdTime": 0,  "speed": 1.0, "floorPosition": 5.72 },
                    { "type": 3, "time": 256, "positionX": -3.0, "holdTime": 48, "speed": 1.0, "floorPosition": 7.04 },
                    { "type": 1, "time": 336, "positionX": -1.8, "holdTime": 0,  "speed": 1.0, "floorPosition": 9.24 },
                    { "type": 4, "time": 336, "positionX":  2.2, "holdTime": 0,  "speed": 1.0, "floorPosition": 9.24 }
                ],
                "notesBelow": [
                    { "type": 1, "time": 384, "positionX":  1.4, "holdTime": 0,  "speed": 1.0, "floorPosition": 10.56 }
                ]
            },
            {
                "bpm": 150.0,
                "speedEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "value": 2.0 }
                ],
                "judgeLineMoveEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 0.5, "end": 0.6, "start2": 0.55, "end2": 0.65 }
                ],
                "judgeLineRotateEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 0.0, "end": 0.0 }
                ],
                "judgeLineDisappearEvents": [
                    { "startTime": 0.0, "endTime": 1000000.0, "start": 1.0, "end": 1.0 }
                ],
                "notesAbove": [
                    { "type": 4, "time": 64,  "positionX":  2.4, "holdTime": 0,  "speed": 1.0, "floorPosition": 1.60 },
                    { "type": 2, "time": 128, "positionX": -1.6, "holdTime": 0,  "speed": 1.0, "floorPosition": 3.20 },
                    { "type": 2, "time": 140, "positionX": -1.6, "holdTime": 0,  "speed": 1.0, "floorPosition": 3.50 },
                    { "type": 4, "time": 304, "positionX":  2.6, "holdTime": 0,  "speed": 1.0, "floorPosition": 7.60 },
                    { "type": 3, "time": 416, "positionX":  0.8, "holdTime": 32, "speed": 1.0, "floorPosition": 10.40 }
                ],
                "notesBelow": []
            }
        ]
    })json";

    REQUIRE(!ChartConverter::ConvertPhigrosOfficialJson("{ broken json").has_value());

    const auto chart = ChartConverter::ConvertPhigrosOfficialJson(officialV3Json, 150.0f);
    REQUIRE(chart.has_value());

    ChartLoader loader;
    REQUIRE(loader.ValidateChartData(*chart));
    REQUIRE(chart->version == 4);
    REQUIRE(!chart->keyboardNotes.empty());
    REQUIRE(!chart->mouseNotes.empty());

    const bool hasKeyboardHold = std::any_of(chart->keyboardNotes.begin(), chart->keyboardNotes.end(),
        [](const KeyboardNote& n) { return n.type == NoteType::Hold && n.duration > 0; });
    REQUIRE(hasKeyboardHold);

    // Verify converted mouse notes stay inside normalized bounds and enforce anti-overlap spacing
    for (std::size_t i = 0; i < chart->mouseNotes.size(); ++i)
    {
        const auto& curr = chart->mouseNotes[i];
        REQUIRE(curr.x >= 0.0f);
        REQUIRE(curr.x <= 1.0f);
        REQUIRE(curr.y >= 0.0f);
        REQUIRE(curr.y <= 1.0f);
        for (std::size_t j = 0; j < i; ++j)
        {
            if (std::abs(curr.time - chart->mouseNotes[j].time) <= 350)
            {
                const double dist = std::hypot(static_cast<double>(curr.x - chart->mouseNotes[j].x),
                                               static_cast<double>(curr.y - chart->mouseNotes[j].y));
                REQUIRE(dist >= 0.06);
            }
        }
    }

    // Also verify OptimizeMouseSpatialLayout expands long-dt jumps and separates stacked short-dt notes
    std::vector<MouseNote> layoutNotes(4);
    layoutNotes[0].time = 500;  layoutNotes[0].x = 0.50f; layoutNotes[0].y = 0.50f;
    layoutNotes[1].time = 620;  layoutNotes[1].x = 0.50f; layoutNotes[1].y = 0.50f;
    layoutNotes[2].time = 740;  layoutNotes[2].x = 0.50f; layoutNotes[2].y = 0.50f;
    layoutNotes[3].time = 2100; layoutNotes[3].x = 0.50f; layoutNotes[3].y = 0.50f;
    ChartConverter::OptimizeMouseSpatialLayout(layoutNotes);
    const double shortJump = std::hypot(static_cast<double>(layoutNotes[1].x - layoutNotes[0].x),
                                        static_cast<double>(layoutNotes[1].y - layoutNotes[0].y));
    const double longJump  = std::hypot(static_cast<double>(layoutNotes[3].x - layoutNotes[2].x),
                                        static_cast<double>(layoutNotes[3].y - layoutNotes[2].y));
    REQUIRE(shortJump >= 0.08 - 1e-4);
    REQUIRE(longJump >= shortJump);
}

TEST_CASE("ChartConverter ConvertPhiraRpeJson converts piecewise BPMList beat triplets, filters fake notes, and emits valid synergies and stageEvents", "[v170][converter][rpe]")
{
    const char* rpeJson = R"json({
        "META": {
            "RPEVersion": 150,
            "name": "Sakura RPE Horizon",
            "composer": "Atelier Kotori",
            "charter": "RPE Architect",
            "song": "music.ogg",
            "background": "cover.png",
            "level": "IN Lv.13.5",
            "offset": 0,
            "id": "sakura_rpe_horizon"
        },
        "BPMList": [
            { "startTime": [0, 0, 1], "bpm": 140.0 },
            { "startTime": [4, 0, 1], "bpm": 175.0 }
        ],
        "judgeLineList": [
            {
                "Name": "PrimaryRhythm",
                "bpmfactor": 1.0,
                "eventLayers": [
                    {
                        "moveXEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": 0.0, "end": 0.0, "easingType": 1 }],
                        "moveYEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": -240.0, "end": -240.0, "easingType": 1 }],
                        "rotateEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": 0.0, "end": 0.0, "easingType": 1 }],
                        "alphaEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": 255, "end": 255, "easingType": 1 }],
                        "speedEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": 10.0, "end": 10.0, "easingType": 1 }]
                    }
                ],
                "notes": [
                    { "type": 1, "startTime": [1, 3, 4],  "endTime": [1, 3, 4],  "positionX": -360.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 2, "startTime": [2, 0, 1],  "endTime": [3, 1, 2],  "positionX": -120.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 1, "startTime": [4, 0, 1],  "endTime": [4, 0, 1],  "positionX":  120.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 2, "startTime": [5, 0, 1],  "endTime": [6, 1, 2],  "positionX":  360.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 1, "startTime": [7, 1, 2],  "endTime": [7, 1, 2],  "positionX": -240.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 1, "startTime": [10, 0, 1], "endTime": [10, 0, 1], "positionX": -300.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 1, "startTime": [10, 0, 1], "endTime": [10, 0, 1], "positionX":  300.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 1, "startTime": [40, 0, 1], "endTime": [40, 0, 1], "positionX":    0.0, "above": 1, "isFake": 1, "speed": 1.0, "size": 1.0, "alpha": 255 }
                ]
            },
            {
                "Name": "SpatialAccent",
                "bpmfactor": 1.0,
                "eventLayers": [
                    {
                        "moveXEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": -120.0, "end": 180.0, "easingType": 1 }],
                        "moveYEvents": [{ "startTime": [0, 0, 1], "endTime": [16, 0, 1], "start": 80.0, "end": 160.0, "easingType": 1 }]
                    }
                ],
                "notes": [
                    { "type": 3, "startTime": [1, 3, 4],  "endTime": [1, 3, 4],  "positionX":  220.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 4, "startTime": [4, 0, 1],  "endTime": [4, 0, 1],  "positionX": -200.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 4, "startTime": [4, 1, 2],  "endTime": [4, 1, 2],  "positionX":  -60.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 4, "startTime": [5, 0, 1],  "endTime": [5, 0, 1],  "positionX":  100.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 3, "startTime": [7, 1, 2],  "endTime": [7, 1, 2],  "positionX":  260.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 3, "startTime": [12, 0, 1], "endTime": [12, 0, 1], "positionX":  140.0, "above": 1, "isFake": 0, "speed": 1.0, "size": 1.0, "alpha": 255 },
                    { "type": 3, "startTime": [42, 0, 1], "endTime": [42, 0, 1], "positionX":  100.0, "above": 1, "isFake": 1, "speed": 1.0, "size": 1.0, "alpha": 255 }
                ]
            }
        ]
    })json";

    ChartInfo   meta;
    std::string levelText;
    const auto  chart = ChartConverter::ConvertPhiraRpeJson(rpeJson, &meta, &levelText);
    REQUIRE(chart.has_value());
    REQUIRE(meta.title == "Sakura RPE Horizon");
    REQUIRE(meta.artist == "Atelier Kotori");
    REQUIRE(meta.charter == "RPE Architect");

    ChartLoader loader;
    REQUIRE(loader.ValidateChartData(*chart));
    REQUIRE(chart->version == 4);

    // Verify piecewise BPMList ([0,0,1]@140 -> [4,0,1]@175 => 4 * 60000 / 140 = 1714.29ms)
    REQUIRE(chart->timingPoints.size() >= 2);
    REQUIRE(chart->timingPoints[0].time == 0);
    REQUIRE_THAT(chart->timingPoints[0].bpm, WithinAbs(140.0, 0.1));
    REQUIRE_THAT(chart->timingPoints[1].time, WithinAbs(1714.0, 3.0));
    REQUIRE_THAT(chart->timingPoints[1].bpm, WithinAbs(175.0, 0.1));

    // Collect all converted timestamps; verify fake notes at [40,0,1] & [42,0,1] (>14000ms) are excluded
    std::vector<int> allTimes;
    std::set<int>    kbLinkIds;
    std::set<int>    msLinkIds;
    for (const auto& kn : chart->keyboardNotes)
    {
        REQUIRE(kn.time < 8000);
        allTimes.push_back(kn.time);
        if (kn.linkId > 0)
            kbLinkIds.insert(kn.linkId);
    }
    for (const auto& mn : chart->mouseNotes)
    {
        REQUIRE(mn.time < 8000);
        allTimes.push_back(mn.time);
        if (mn.linkId > 0)
            msLinkIds.insert(mn.linkId);
    }
    REQUIRE(!chart->keyboardNotes.empty());
    REQUIRE(!chart->mouseNotes.empty());

    // [1,3,4] @ 140 BPM = 750ms; [4,0,1] @ 140 BPM = 1714ms; [7,1,2] = 1714.29 + 3.5*(60000/175) = 2914ms
    REQUIRE(HasTimestampNear(allTimes, 750, 4));
    REQUIRE(HasTimestampNear(allTimes, 1714, 4));
    REQUIRE(HasTimestampNear(allTimes, 2914, 4));

    // Verify cross-zone linkId synergy pairing
    REQUIRE(!kbLinkIds.empty());
    REQUIRE(!msLinkIds.empty());
    bool foundSharedLink = false;
    for (int id : msLinkIds)
    {
        if (kbLinkIds.count(id) > 0)
            foundSharedLink = true;
    }
    REQUIRE(foundSharedLink);

    // Verify Chart v4 stageEvents
    REQUIRE(!chart->stageEvents.empty());
    for (const auto& ev : chart->stageEvents)
    {
        REQUIRE(ev.time >= 0);
        REQUIRE(ev.duration > 0);
        REQUIRE(!ev.type.empty());
        REQUIRE(ev.intensity > 0.0f);
    }
}

TEST_CASE("ChartConverter ImportExternalPackage imports .pez, .osz, and .sm packages end-to-end", "[v170][converter][import]")
{
    ScopedV170TempEnv env;
    const fs::path destChartsRoot = env.dir / "user_charts";
    fs::create_directories(destChartsRoot);
    ChartLoader loader;

    // 1. Phira .pez package (info.yml + RPE chart JSON + dummy .ogg + cover .png, Deflate ZIP)
    {
        const char* infoYml = R"yaml(name: Azure Petal Reverie
composer: Kotori Soundworks
charter: Phira Charter
song: music.ogg
picture: cover.png
chart: chart.json
level: IN Lv.12.5
previewStart: 1.0
)yaml";

        const char* pezRpeJson = R"json({
            "META": {
                "RPEVersion": 150,
                "name": "Azure Petal Reverie",
                "composer": "Kotori Soundworks",
                "charter": "Phira Charter",
                "song": "music.ogg",
                "background": "cover.png",
                "level": "IN Lv.12.5",
                "offset": 0
            },
            "BPMList": [{ "startTime": [0, 0, 1], "bpm": 160.0 }],
            "judgeLineList": [
                {
                    "Name": "Main",
                    "bpmfactor": 1.0,
                    "eventLayers": [{}],
                    "notes": [
                        { "type": 1, "startTime": [1, 0, 1], "endTime": [1, 0, 1], "positionX": -300.0, "above": 1, "isFake": 0 },
                        { "type": 2, "startTime": [2, 0, 1], "endTime": [3, 0, 1], "positionX":  100.0, "above": 1, "isFake": 0 },
                        { "type": 3, "startTime": [4, 0, 1], "endTime": [4, 0, 1], "positionX":  280.0, "above": 1, "isFake": 0 },
                        { "type": 4, "startTime": [5, 0, 1], "endTime": [5, 0, 1], "positionX": -150.0, "above": 1, "isFake": 0 },
                        { "type": 1, "startTime": [6, 0, 1], "endTime": [6, 0, 1], "positionX":  200.0, "above": 1, "isFake": 0 }
                    ]
                }
            ]
        })json";

        const fs::path pezPath = env.dir / "azure_petal_reverie.pez";
        REQUIRE(WriteCustomZipArchive(pezPath, {
            MakeDeflateZipEntry("info.yml", infoYml),
            MakeDeflateZipEntry("chart.json", pezRpeJson),
            MakeDeflateZipEntry("music.ogg", MakeDummyOggPayload()),
            MakeDeflateZipEntry("cover.png", MakeMinimalPngPayload()),
        }));

        REQUIRE(ChartConverter::DetectFormat(pezPath) == ExternalChartFormat::PhiraPez);
        const auto res = ChartConverter::ImportExternalPackage(pezPath, destChartsRoot);
        REQUIRE(res.ok);
        REQUIRE(res.format == ExternalChartFormat::PhiraPez);
        REQUIRE(res.convertedDifficulties >= 1);
        REQUIRE(!res.outputChartDir.empty());

        const auto info = loader.LoadChartInfo((fs::path(res.outputChartDir) / "info.json").string());
        REQUIRE(info.has_value());
        REQUIRE(info->title == "Azure Petal Reverie");
        REQUIRE(fs::is_regular_file(fs::path(info->folderPath) / info->musicFile));
        for (const auto& diff : info->difficulties)
        {
            const auto data = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));
        }
    }

    // 2. osu! .osz package (beatmap_4k.osu Mode 3 + beatmap_std.osu Mode 0 + dummy audio)
    {
        const char* osuMania4k = R"osu(osu file format v14

[General]
AudioFilename: audio.ogg
AudioLeadIn: 0
PreviewTime: 1000
Mode: 3

[Metadata]
Title:Crimson Blossom Dual
Artist:Sakura Sound Team
Creator:ManiaMapper
Version:4K MX

[Difficulty]
HPDrainRate:6
CircleSize:4
OverallDifficulty:8
ApproachRate:9
SliderMultiplier:1.4
SliderTickRate:1

[Events]
0,0,"bg.png",0,0

[TimingPoints]
0,400,4,2,0,80,1,0
1600,-80,4,2,0,80,0,0

[HitObjects]
64,192,400,1,0,0:0:0:0:
192,192,600,1,0,0:0:0:0:
320,192,800,128,0,1200:0:0:0:0:
448,192,1400,1,2,0:0:0:0:
64,192,1600,1,0,0:0:0:0:
448,192,1600,1,8,0:0:0:0:
192,192,2000,128,0,2400:0:0:0:0:
)osu";

        const char* osuStd = R"osu(osu file format v14

[General]
AudioFilename: audio.ogg
AudioLeadIn: 0
PreviewTime: 1000
Mode: 0

[Metadata]
Title:Crimson Blossom Dual
Artist:Sakura Sound Team
Creator:StdMapper
Version:Insane

[Difficulty]
HPDrainRate:5.5
CircleSize:4
OverallDifficulty:8.5
ApproachRate:9.2
SliderMultiplier:1.6
SliderTickRate:1

[Events]
0,0,"bg.png",0,0

[TimingPoints]
0,400,4,2,0,80,1,0

[HitObjects]
128,160,400,1,0,0:0:0:0:
256,192,800,2,0,L|384:240,1,160
384,128,1400,1,2,0:0:0:0:
160,280,1800,2,0,B|240:200|320:280,1,224
256,192,2400,1,8,0:0:0:0:
)osu";

        const fs::path oszPath = env.dir / "crimson_blossom_dual.osz";
        REQUIRE(WriteCustomZipArchive(oszPath, {
            MakeDeflateZipEntry("beatmap_4k.osu", osuMania4k),
            MakeDeflateZipEntry("beatmap_std.osu", osuStd),
            MakeDeflateZipEntry("audio.ogg", MakeDummyOggPayload()),
            MakeDeflateZipEntry("bg.png", MakeMinimalPngPayload()),
        }));

        REQUIRE(ChartConverter::DetectFormat(oszPath) == ExternalChartFormat::OsuArchive);
        const auto res = ChartConverter::ImportExternalPackage(oszPath, destChartsRoot);
        REQUIRE(res.ok);
        REQUIRE(res.format == ExternalChartFormat::OsuArchive);
        REQUIRE(res.convertedDifficulties == 2);

        const auto info = loader.LoadChartInfo((fs::path(res.outputChartDir) / "info.json").string());
        REQUIRE(info.has_value());
        REQUIRE(info->difficulties.size() == 2);
        REQUIRE(fs::is_regular_file(fs::path(info->folderPath) / info->musicFile));
        for (const auto& diff : info->difficulties)
        {
            const auto data = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));
            REQUIRE(!data->keyboardNotes.empty());
            REQUIRE(!data->mouseNotes.empty());
        }
    }

    // 3. StepMania .sm directory/file (#TITLE, #BPMS, 2 dance-single difficulties + dummy audio)
    {
        const char* smText = R"sm(#TITLE:Moonlit Step Cascade;
#ARTIST:Sakura Step Lab;
#CREDIT:StepArchitect;
#MUSIC:music.ogg;
#OFFSET:0.000;
#SAMPLESTART:1.500;
#BPMS:0.000=150.000,8.000=180.000;
#NOTES:
     dance-single:
     StepArchitect:
     Medium:
     6:
     0.5,0.5,0.5,0.5,0.5:
1000
0100
0010
0001
,
2001
0100
3010
0000
;
#NOTES:
     dance-single:
     StepArchitect:
     Challenge:
     11:
     0.8,0.8,0.8,0.8,0.8:
1001
0110
1010
0101
0010
1000
0100
1001
,
2002
0100
0010
3003
;
)sm";

        const fs::path smDir  = env.dir / "moonlit_step_cascade";
        const fs::path smFile = smDir / "moonlit_step_cascade.sm";
        fs::create_directories(smDir);
        REQUIRE(sakura::utils::AtomicWrite(smFile, smText));
        REQUIRE(sakura::utils::AtomicWrite(smDir / "music.ogg", MakeDummyOggPayload()));

        REQUIRE(ChartConverter::DetectFormat(smFile) == ExternalChartFormat::StepManiaSm);
        const auto res = ChartConverter::ImportExternalPackage(smFile, destChartsRoot);
        REQUIRE(res.ok);
        REQUIRE(res.format == ExternalChartFormat::StepManiaSm);
        REQUIRE(res.convertedDifficulties == 2);

        const auto info = loader.LoadChartInfo((fs::path(res.outputChartDir) / "info.json").string());
        REQUIRE(info.has_value());
        REQUIRE(info->title == "Moonlit Step Cascade");
        REQUIRE(info->difficulties.size() == 2);
        REQUIRE(fs::is_regular_file(fs::path(info->folderPath) / info->musicFile));
        for (const auto& diff : info->difficulties)
        {
            const auto data = loader.LoadChartData(info->folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));
            REQUIRE(!data->keyboardNotes.empty());
            REQUIRE(!data->mouseNotes.empty());
        }
    }
}

TEST_CASE("Built-in chart library contains all 15 authentic OGG songs including Vol.04 and zero .wav placeholder songs", "[v170][charts][vol4]")
{
    const fs::path chartsRoot = fs::path(SAKURA_SOURCE_DIR) / "resources" / "charts";
    ChartLoader    loader;
    const auto     charts = loader.ScanCharts(chartsRoot.string());
    REQUIRE(charts.size() == 15);

    constexpr std::array<const char*, 6> kVol4Ids{{
        "moonlit_prelude",
        "sakura_memories",
        "sakura_pulse",
        "sakura_sprint",
        "shattered_drive_f",
        "shattered_threshold",
    }};
    for (const char* vol4Id : kVol4Ids)
    {
        const auto it = std::find_if(charts.begin(), charts.end(),
            [vol4Id](const ChartInfo& c) { return c.id == vol4Id; });
        REQUIRE(it != charts.end());
        REQUIRE(it->version == 4);
        REQUIRE(it->musicFile == "music.ogg");
        REQUIRE(it->coverFile == "cover.png");
        REQUIRE(fs::is_regular_file(fs::path(it->folderPath) / it->coverFile));
    }

    for (const auto& chart : charts)
    {
        REQUIRE(chart.musicFile == "music.ogg");
        const fs::path musicPath = fs::path(chart.folderPath) / chart.musicFile;
        REQUIRE(fs::is_regular_file(musicPath));
        REQUIRE(fs::file_size(musicPath) > 1024);

        std::ifstream oggIn(musicPath, std::ios::binary);
        char magic[4] = {};
        REQUIRE(static_cast<bool>(oggIn.read(magic, 4)));
        REQUIRE(std::string_view(magic, 4) == "OggS");

        REQUIRE(!chart.difficulties.empty());
        for (const auto& diff : chart.difficulties)
        {
            const auto data = loader.LoadChartData(chart.folderPath + "/" + diff.chartFile);
            REQUIRE(data.has_value());
            REQUIRE(loader.ValidateChartData(*data));
        }
    }

    // Ensure zero .wav files exist anywhere inside resources/charts
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(chartsRoot, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        REQUIRE(ext != ".wav");
    }
    REQUIRE(!ec);
}
