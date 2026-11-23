#include "test_framework.h"
#include "utils/archive.h"
#include "utils/file_io.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("CreateZipArchive and ExtractZipArchive round-trip .sakura chart package safely", "[archive][sakura]")
{
    const fs::path root = fs::temp_directory_path() / "sakura-archive-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    const fs::path srcDir = root / "src_chart";
    const fs::path pkgFile = root / "test_chart.sakura";
    const fs::path dstDir = root / "extracted";
    fs::create_directories(srcDir / "sub");

    REQUIRE(sakura::utils::AtomicWrite(srcDir / "info.json", "{\"version\":3,\"id\":\"demo\"}\n"));
    REQUIRE(sakura::utils::AtomicWrite(srcDir / "normal.json", "{\"version\":3,\"timing_points\":[{\"time\":0,\"bpm\":120}]}\n"));
    REQUIRE(sakura::utils::AtomicWrite(srcDir / "sub" / "readme.txt", "Sakura Package Content"));

    REQUIRE(sakura::utils::CreateZipArchive(srcDir, pkgFile));
    REQUIRE(fs::is_regular_file(pkgFile));
    REQUIRE(sakura::utils::ExtractZipArchive(pkgFile, dstDir));

    std::ifstream infoIn(dstDir / "info.json");
    std::string infoText((std::istreambuf_iterator<char>(infoIn)), std::istreambuf_iterator<char>());
    REQUIRE(infoText.find("\"demo\"") != std::string::npos);
    REQUIRE(fs::is_regular_file(dstDir / "sub" / "readme.txt"));

    // Corrupted CRC must fail extraction
    std::fstream corrupt(pkgFile, std::ios::in | std::ios::out | std::ios::binary);
    corrupt.seekp(40);
    char b = 0;
    corrupt.read(&b, 1);
    corrupt.seekp(40);
    b ^= 0x55;
    corrupt.write(&b, 1);
    corrupt.close();
    REQUIRE(!sakura::utils::ExtractZipArchive(pkgFile, root / "bad_out"));

    fs::remove_all(root, ec);
}
