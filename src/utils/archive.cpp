#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace sakura::utils
{

namespace
{

std::array<std::uint32_t, 256> BuildCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
        table[i] = c;
    }
    return table;
}

void AppendU16(std::string& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFFU));
    out.push_back(static_cast<char>((v >> 8U) & 0xFFU));
}

void AppendU32(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFFU));
    out.push_back(static_cast<char>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((v >> 24U) & 0xFFU));
}

std::uint16_t ReadU16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8U);
}

std::uint32_t ReadU32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8U)
        | (static_cast<std::uint32_t>(p[2]) << 16U)
        | (static_cast<std::uint32_t>(p[3]) << 24U);
}

bool IsSafeRelativePath(const std::string& rawName)
{
    if (rawName.empty() || rawName.find(':') != std::string::npos || rawName[0] == '/' || rawName[0] == '\\')
        return false;
    std::string normalized = rawName;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const fs::path p(normalized);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
        return false;
    for (const auto& part : p)
    {
        if (part == ".." || part.empty())
            return false;
    }
    return true;
}

struct CentralEntry
{
    std::string   name;
    std::uint32_t crc32  = 0;
    std::uint32_t size   = 0;
    std::uint32_t offset = 0;
};

} // namespace

std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t length)
{
    static const auto kTable = BuildCrcTable();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i)
        crc = kTable[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8U);
    return crc ^ 0xFFFFFFFFU;
}

bool CreateZipArchive(const fs::path& sourceDir, const fs::path& archivePath)
{
    std::error_code ec;
    if (!fs::is_directory(sourceDir, ec))
        return false;

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(sourceDir, ec))
    {
        if (ec || entry.is_symlink(ec))
            return false;
        if (entry.is_regular_file(ec))
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty() || files.size() > 65535)
        return false;

    std::string blob;
    blob.reserve(1024 * 1024);
    std::vector<CentralEntry> central;
    central.reserve(files.size());

    for (const auto& filePath : files)
    {
        const std::string relName = fs::relative(filePath, sourceDir, ec).generic_string();
        if (ec || !IsSafeRelativePath(relName) || relName.size() > 4096)
            return false;

        std::ifstream in(filePath, std::ios::binary);
        if (!in)
            return false;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.size() > 0x7FFFFFFFULL)
            return false;

        const auto crc = ComputeCrc32(reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
        const auto sz  = static_cast<std::uint32_t>(content.size());
        const auto off = static_cast<std::uint32_t>(blob.size());

        // Local file header (0x04034b50)
        AppendU32(blob, 0x04034b50U);
        AppendU16(blob, 20);      // version needed
        AppendU16(blob, 0x0800U); // UTF-8 filename flag
        AppendU16(blob, 0);       // method 0 = Store
        AppendU16(blob, 0);       // mod time
        AppendU16(blob, 0);       // mod date
        AppendU32(blob, crc);
        AppendU32(blob, sz);
        AppendU32(blob, sz);
        AppendU16(blob, static_cast<std::uint16_t>(relName.size()));
        AppendU16(blob, 0);
        blob.append(relName);
        blob.append(content);

        central.push_back({ relName, crc, sz, off });
    }

    const auto cdStart = static_cast<std::uint32_t>(blob.size());
    for (const auto& ce : central)
    {
        AppendU32(blob, 0x02014b50U);
        AppendU16(blob, 20);
        AppendU16(blob, 20);
        AppendU16(blob, 0x0800U);
        AppendU16(blob, 0);
        AppendU16(blob, 0);
        AppendU16(blob, 0);
        AppendU32(blob, ce.crc32);
        AppendU32(blob, ce.size);
        AppendU32(blob, ce.size);
        AppendU16(blob, static_cast<std::uint16_t>(ce.name.size()));
        AppendU16(blob, 0);
        AppendU16(blob, 0);
        AppendU16(blob, 0);
        AppendU16(blob, 0);
        AppendU32(blob, 0);
        AppendU32(blob, ce.offset);
        blob.append(ce.name);
    }
    const auto cdSize = static_cast<std::uint32_t>(blob.size()) - cdStart;

    // End of central directory (0x06054b50)
    AppendU32(blob, 0x06054b50U);
    AppendU16(blob, 0);
    AppendU16(blob, 0);
    AppendU16(blob, static_cast<std::uint16_t>(central.size()));
    AppendU16(blob, static_cast<std::uint16_t>(central.size()));
    AppendU32(blob, cdSize);
    AppendU32(blob, cdStart);
    AppendU16(blob, 0);

    if (archivePath.has_parent_path())
        fs::create_directories(archivePath.parent_path(), ec);
    return AtomicWrite(archivePath, blob);
}

bool ExtractZipArchive(const fs::path& archivePath, const fs::path& destDir, std::uintmax_t maxTotalBytes)
{
    std::error_code ec;
    const auto fsize = fs::file_size(archivePath, ec);
    if (ec || fsize < 22 || fsize > maxTotalBytes)
        return false;

    std::ifstream in(archivePath, std::ios::binary);
    if (!in)
        return false;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(fsize));
    if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size())))
        return false;

    // Locate EOCD signature from tail
    std::size_t eocdPos = buf.size();
    const std::size_t minSearch = buf.size() > 65557 ? (buf.size() - 65557) : 0;
    for (std::size_t p = buf.size() - 22; ; --p)
    {
        if (ReadU32(&buf[p]) == 0x06054b50U)
        {
            eocdPos = p;
            break;
        }
        if (p == minSearch)
            break;
    }
    if (eocdPos == buf.size())
        return false;

    const std::uint16_t entryCount = ReadU16(&buf[eocdPos + 10]);
    const std::uint32_t cdSize     = ReadU32(&buf[eocdPos + 12]);
    const std::uint32_t cdOffset   = ReadU32(&buf[eocdPos + 16]);
    if (entryCount == 0 || static_cast<std::uint64_t>(cdOffset) + cdSize > eocdPos)
        return false;

    fs::create_directories(destDir, ec);
    if (ec)
        return false;

    std::size_t pos = cdOffset;
    std::uintmax_t extractedTotal = 0;
    for (std::uint16_t i = 0; i < entryCount; ++i)
    {
        if (pos + 46 > eocdPos || ReadU32(&buf[pos]) != 0x02014b50U)
            return false;
        const std::uint16_t method    = ReadU16(&buf[pos + 10]);
        const std::uint32_t crc       = ReadU32(&buf[pos + 16]);
        const std::uint32_t compSz    = ReadU32(&buf[pos + 20]);
        const std::uint32_t uncompSz  = ReadU32(&buf[pos + 24]);
        const std::uint16_t nameLen   = ReadU16(&buf[pos + 28]);
        const std::uint16_t extraLen  = ReadU16(&buf[pos + 30]);
        const std::uint16_t commLen   = ReadU16(&buf[pos + 32]);
        const std::uint32_t localOff  = ReadU32(&buf[pos + 42]);

        if (pos + 46 + nameLen + extraLen + commLen > eocdPos)
            return false;
        std::string rawName(reinterpret_cast<const char*>(&buf[pos + 46]), nameLen);
        pos += 46 + nameLen + extraLen + commLen;

        if (!rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\'))
            continue;
        if (method != 0 || compSz != uncompSz || !IsSafeRelativePath(rawName))
            return false;
        extractedTotal += uncompSz;
        if (extractedTotal > maxTotalBytes)
            return false;

        if (static_cast<std::uint64_t>(localOff) + 30 > buf.size() || ReadU32(&buf[localOff]) != 0x04034b50U)
            return false;
        const std::uint16_t lNameLen  = ReadU16(&buf[localOff + 26]);
        const std::uint16_t lExtraLen = ReadU16(&buf[localOff + 28]);
        const std::size_t dataStart   = static_cast<std::size_t>(localOff) + 30 + lNameLen + lExtraLen;
        if (dataStart + compSz > buf.size())
            return false;

        const auto actualCrc = ComputeCrc32(&buf[dataStart], compSz);
        if (actualCrc != crc)
            return false;

        std::replace(rawName.begin(), rawName.end(), '\\', '/');
        const fs::path relPath(std::u8string(reinterpret_cast<const char8_t*>(rawName.data()), rawName.size()));
        const fs::path outPath = destDir / relPath;
        if (outPath.has_parent_path())
            fs::create_directories(outPath.parent_path(), ec);
        std::string content(reinterpret_cast<const char*>(&buf[dataStart]), compSz);
        if (!AtomicWrite(outPath, content))
            return false;
    }
    return true;
}

} // namespace sakura::utils
