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

std::string ComputeSha256Hex(const std::uint8_t* data, std::size_t length)
{
    static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    auto rotr = [](std::uint32_t x, std::uint32_t n) -> std::uint32_t {
        return (x >> n) | (x << (32U - n));
    };

    std::array<std::uint32_t, 8> state = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    const std::uint64_t bitLen = static_cast<std::uint64_t>(length) * 8ULL;
    std::vector<std::uint8_t> msg;
    msg.reserve(((length + 9 + 63) / 64) * 64);
    if (data && length > 0)
        msg.insert(msg.end(), data, data + length);
    msg.push_back(0x80U);
    while ((msg.size() % 64) != 56)
        msg.push_back(0x00U);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFU));

    std::array<std::uint32_t, 64> w{};
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64)
    {
        for (std::size_t i = 0; i < 16; ++i)
        {
            const std::size_t j = chunk + i * 4;
            w[i] = (static_cast<std::uint32_t>(msg[j]) << 24U)
                 | (static_cast<std::uint32_t>(msg[j + 1]) << 16U)
                 | (static_cast<std::uint32_t>(msg[j + 2]) << 8U)
                 | static_cast<std::uint32_t>(msg[j + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7U) ^ rotr(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
            const std::uint32_t s1 = rotr(w[i - 2], 17U) ^ rotr(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (std::size_t i = 0; i < 64; ++i)
        {
            const std::uint32_t S1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + S1 + ch + kRoundConstants[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e;
            e = d + temp1;
            d = c; c = b; b = a;
            a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(kHex[(word >> shift) & 0xFU]);
    }
    return out;
}

std::string ComputeSha256Hex(std::string_view data)
{
    return ComputeSha256Hex(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
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
