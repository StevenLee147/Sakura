#include "utils/archive.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
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

struct DeflateBitReader
{
    const std::uint8_t* data     = nullptr;
    std::size_t         size     = 0;
    std::size_t         pos      = 0;
    std::uint64_t       bitBuf   = 0;
    int                 bitCount = 0;

    void Refill()
    {
        while (bitCount <= 56 && pos < size)
        {
            bitBuf |= static_cast<std::uint64_t>(data[pos++]) << bitCount;
            bitCount += 8;
        }
    }

    void Consume(int n)
    {
        bitBuf >>= n;
        bitCount -= n;
    }

    bool ReadBits(int n, std::uint32_t& out)
    {
        if (n == 0)
        {
            out = 0;
            return true;
        }
        if (bitCount < n)
            Refill();
        if (bitCount < n)
            return false;
        out = static_cast<std::uint32_t>(bitBuf & ((1ULL << n) - 1ULL));
        bitBuf >>= n;
        bitCount -= n;
        return true;
    }

    void AlignToByte()
    {
        const int discard = bitCount & 7;
        if (discard > 0)
        {
            bitBuf >>= discard;
            bitCount -= discard;
        }
        pos -= static_cast<std::size_t>(bitCount / 8);
        bitBuf = 0;
        bitCount = 0;
    }

    bool ReadAlignedU16(std::uint16_t& out)
    {
        if (size - pos < 2)
            return false;
        out = ReadU16(data + pos);
        pos += 2;
        return true;
    }

    bool AppendAlignedBytes(std::size_t count, std::string& out)
    {
        if (size - pos < count)
            return false;
        if (count > 0)
            out.append(reinterpret_cast<const char*>(data + pos), count);
        pos += count;
        return true;
    }
};

struct HuffmanTable
{
    std::vector<std::uint16_t> entries;
    int                        tableBits = 0;
};

std::uint32_t ReverseBits(std::uint32_t code, int len)
{
    std::uint32_t rev = 0;
    for (int i = 0; i < len; ++i)
    {
        rev = (rev << 1U) | (code & 1U);
        code >>= 1U;
    }
    return rev;
}

bool BuildHuffmanTable(const std::uint8_t* lengths, std::size_t numSymbols, HuffmanTable& outTable)
{
    std::array<int, 16> blCount{};
    int maxLen = 0;
    for (std::size_t i = 0; i < numSymbols; ++i)
    {
        const std::uint8_t len = lengths[i];
        if (len > 15U)
            return false;
        if (len > 0U)
        {
            ++blCount[len];
            if (static_cast<int>(len) > maxLen)
                maxLen = static_cast<int>(len);
        }
    }

    if (maxLen == 0)
    {
        outTable.entries.clear();
        outTable.tableBits = 0;
        return true;
    }

    int left = 1;
    for (int bits = 1; bits <= maxLen; ++bits)
    {
        left <<= 1;
        left -= blCount[bits];
        if (left < 0)
            return false;
    }

    std::array<std::uint32_t, 16> nextCode{};
    std::uint32_t code = 0;
    for (int bits = 1; bits <= maxLen; ++bits)
    {
        code = (code + static_cast<std::uint32_t>(blCount[bits - 1])) << 1U;
        nextCode[bits] = code;
    }

    const std::uint32_t tableSize = 1U << maxLen;
    outTable.entries.assign(tableSize, 0U);
    outTable.tableBits = maxLen;

    for (std::size_t sym = 0; sym < numSymbols; ++sym)
    {
        const int len = static_cast<int>(lengths[sym]);
        if (len == 0)
            continue;
        const std::uint32_t msbCode = nextCode[len]++;
        const std::uint32_t revCode = ReverseBits(msbCode, len);
        const auto packed = static_cast<std::uint16_t>((sym << 5U) | static_cast<std::size_t>(len));
        const std::uint32_t step = 1U << len;
        for (std::uint32_t idx = revCode; idx < tableSize; idx += step)
            outTable.entries[idx] = packed;
    }
    return true;
}

bool DecodeHuffmanSymbol(DeflateBitReader& reader, const HuffmanTable& table, std::uint32_t& outSym)
{
    if (table.tableBits <= 0)
        return false;
    if (reader.bitCount < table.tableBits)
        reader.Refill();
    const std::uint32_t mask = (1U << table.tableBits) - 1U;
    const std::uint16_t entry = table.entries[static_cast<std::uint32_t>(reader.bitBuf) & mask];
    const int codeLen = static_cast<int>(entry & 0x1FU);
    if (codeLen == 0 || codeLen > reader.bitCount)
        return false;
    outSym = static_cast<std::uint32_t>(entry >> 5U);
    reader.Consume(codeLen);
    return true;
}

const HuffmanTable& GetFixedLitLenTable()
{
    static const HuffmanTable kTable = []() {
        std::array<std::uint8_t, 288> lengths{};
        for (std::size_t i = 0; i <= 143; ++i)
            lengths[i] = 8;
        for (std::size_t i = 144; i <= 255; ++i)
            lengths[i] = 9;
        for (std::size_t i = 256; i <= 279; ++i)
            lengths[i] = 7;
        for (std::size_t i = 280; i <= 287; ++i)
            lengths[i] = 8;
        HuffmanTable t;
        BuildHuffmanTable(lengths.data(), lengths.size(), t);
        return t;
    }();
    return kTable;
}

const HuffmanTable& GetFixedDistTable()
{
    static const HuffmanTable kTable = []() {
        std::array<std::uint8_t, 32> lengths{};
        lengths.fill(5);
        HuffmanTable t;
        BuildHuffmanTable(lengths.data(), lengths.size(), t);
        return t;
    }();
    return kTable;
}

bool InflateHuffmanBlock(DeflateBitReader& reader,
                         const HuffmanTable& litLenTable,
                         const HuffmanTable& distTable,
                         std::string& outUncompressed,
                         std::size_t maxOutputBytes)
{
    static constexpr std::array<std::uint16_t, 29> kLengthBase = {
        3, 4, 5, 6, 7, 8, 9, 10,
        11, 13, 15, 17,
        19, 23, 27, 31,
        35, 43, 51, 59,
        67, 83, 99, 115,
        131, 163, 195, 227,
        258
    };
    static constexpr std::array<std::uint8_t, 29> kLengthExtra = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1,
        2, 2, 2, 2,
        3, 3, 3, 3,
        4, 4, 4, 4,
        5, 5, 5, 5,
        0
    };
    static constexpr std::array<std::uint16_t, 30> kDistBase = {
        1, 2, 3, 4,
        5, 7,
        9, 13,
        17, 25,
        33, 49,
        65, 97,
        129, 193,
        257, 385,
        513, 769,
        1025, 1537,
        2049, 3073,
        4097, 6145,
        8193, 12289,
        16385, 24577
    };
    static constexpr std::array<std::uint8_t, 30> kDistExtra = {
        0, 0, 0, 0,
        1, 1,
        2, 2,
        3, 3,
        4, 4,
        5, 5,
        6, 6,
        7, 7,
        8, 8,
        9, 9,
        10, 10,
        11, 11,
        12, 12,
        13, 13
    };

    for (;;)
    {
        std::uint32_t sym = 0;
        if (!DecodeHuffmanSymbol(reader, litLenTable, sym))
            return false;

        if (sym < 256U)
        {
            if (outUncompressed.size() >= maxOutputBytes)
                return false;
            outUncompressed.push_back(static_cast<char>(sym));
        }
        else if (sym == 256U)
        {
            return true;
        }
        else if (sym <= 285U)
        {
            const std::uint32_t lenIdx = sym - 257U;
            std::uint32_t lenExtra = 0;
            if (!reader.ReadBits(kLengthExtra[lenIdx], lenExtra))
                return false;
            const std::size_t matchLen = static_cast<std::size_t>(kLengthBase[lenIdx]) + lenExtra;

            std::uint32_t distSym = 0;
            if (!DecodeHuffmanSymbol(reader, distTable, distSym) || distSym >= 30U)
                return false;

            std::uint32_t distExtra = 0;
            if (!reader.ReadBits(kDistExtra[distSym], distExtra))
                return false;
            const std::size_t distance = static_cast<std::size_t>(kDistBase[distSym]) + distExtra;

            if (distance == 0 || distance > outUncompressed.size())
                return false;
            if (matchLen > maxOutputBytes - outUncompressed.size())
                return false;

            const std::size_t startOut = outUncompressed.size();
            outUncompressed.resize(startOut + matchLen);
            char* dst = outUncompressed.data() + startOut;
            const char* src = dst - distance;
            if (distance >= matchLen)
            {
                std::memcpy(dst, src, matchLen);
            }
            else if (distance == 1)
            {
                std::memset(dst, static_cast<unsigned char>(*src), matchLen);
            }
            else
            {
                for (std::size_t k = 0; k < matchLen; ++k)
                    dst[k] = src[k];
            }
        }
        else
        {
            return false;
        }
    }
}

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

bool InflateRawDeflate(const std::uint8_t* compData,
                       std::size_t compSize,
                       std::string& outUncompressed,
                       std::size_t maxOutputBytes)
{
    outUncompressed.clear();
    if (compData == nullptr || compSize == 0)
        return false;

    static constexpr std::array<std::uint8_t, 19> kCodeLengthOrder = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    DeflateBitReader reader{ compData, compSize, 0, 0, 0 };
    HuffmanTable codeLenTable;
    HuffmanTable dynLitLenTable;
    HuffmanTable dynDistTable;

    std::uint32_t bfinal = 0;
    do
    {
        std::uint32_t btype = 0;
        if (!reader.ReadBits(1, bfinal) || !reader.ReadBits(2, btype))
        {
            outUncompressed.clear();
            return false;
        }

        if (btype == 0U)
        {
            reader.AlignToByte();
            std::uint16_t len  = 0;
            std::uint16_t nlen = 0;
            if (!reader.ReadAlignedU16(len) || !reader.ReadAlignedU16(nlen))
            {
                outUncompressed.clear();
                return false;
            }
            if ((static_cast<std::uint32_t>(len) ^ static_cast<std::uint32_t>(nlen)) != 0xFFFFU)
            {
                outUncompressed.clear();
                return false;
            }
            if (static_cast<std::size_t>(len) > maxOutputBytes - outUncompressed.size())
            {
                outUncompressed.clear();
                return false;
            }
            if (!reader.AppendAlignedBytes(static_cast<std::size_t>(len), outUncompressed))
            {
                outUncompressed.clear();
                return false;
            }
        }
        else if (btype == 1U)
        {
            if (!InflateHuffmanBlock(reader, GetFixedLitLenTable(), GetFixedDistTable(), outUncompressed, maxOutputBytes))
            {
                outUncompressed.clear();
                return false;
            }
        }
        else if (btype == 2U)
        {
            std::uint32_t hlit  = 0;
            std::uint32_t hdist = 0;
            std::uint32_t hclen = 0;
            if (!reader.ReadBits(5, hlit) || !reader.ReadBits(5, hdist) || !reader.ReadBits(4, hclen))
            {
                outUncompressed.clear();
                return false;
            }

            const std::uint32_t numLitLen  = hlit + 257U;
            const std::uint32_t numDist    = hdist + 1U;
            const std::uint32_t numCodeLen = hclen + 4U;
            if (numLitLen > 286U || numDist > 30U)
            {
                outUncompressed.clear();
                return false;
            }

            std::array<std::uint8_t, 19> codeLenLengths{};
            for (std::uint32_t i = 0; i < numCodeLen; ++i)
            {
                std::uint32_t clVal = 0;
                if (!reader.ReadBits(3, clVal))
                {
                    outUncompressed.clear();
                    return false;
                }
                codeLenLengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(clVal);
            }

            if (!BuildHuffmanTable(codeLenLengths.data(), codeLenLengths.size(), codeLenTable) || codeLenTable.tableBits <= 0)
            {
                outUncompressed.clear();
                return false;
            }

            const std::uint32_t totalCodes = numLitLen + numDist;
            std::array<std::uint8_t, 286 + 30> combinedLengths{};
            std::uint32_t idx = 0;
            while (idx < totalCodes)
            {
                std::uint32_t clSym = 0;
                if (!DecodeHuffmanSymbol(reader, codeLenTable, clSym))
                {
                    outUncompressed.clear();
                    return false;
                }

                if (clSym <= 15U)
                {
                    combinedLengths[idx++] = static_cast<std::uint8_t>(clSym);
                }
                else if (clSym == 16U)
                {
                    if (idx == 0)
                    {
                        outUncompressed.clear();
                        return false;
                    }
                    std::uint32_t extra = 0;
                    if (!reader.ReadBits(2, extra))
                    {
                        outUncompressed.clear();
                        return false;
                    }
                    const std::uint32_t repeat = 3U + extra;
                    if (idx + repeat > totalCodes)
                    {
                        outUncompressed.clear();
                        return false;
                    }
                    const std::uint8_t prev = combinedLengths[idx - 1];
                    for (std::uint32_t r = 0; r < repeat; ++r)
                        combinedLengths[idx++] = prev;
                }
                else if (clSym == 17U || clSym == 18U)
                {
                    std::uint32_t extra = 0;
                    const int extraBits = (clSym == 17U) ? 3 : 7;
                    const std::uint32_t baseRepeat = (clSym == 17U) ? 3U : 11U;
                    if (!reader.ReadBits(extraBits, extra))
                    {
                        outUncompressed.clear();
                        return false;
                    }
                    const std::uint32_t repeat = baseRepeat + extra;
                    if (idx + repeat > totalCodes)
                    {
                        outUncompressed.clear();
                        return false;
                    }
                    for (std::uint32_t r = 0; r < repeat; ++r)
                        combinedLengths[idx++] = 0;
                }
                else
                {
                    outUncompressed.clear();
                    return false;
                }
            }

            if (combinedLengths[256] == 0)
            {
                outUncompressed.clear();
                return false;
            }

            if (!BuildHuffmanTable(combinedLengths.data(), numLitLen, dynLitLenTable) ||
                !BuildHuffmanTable(combinedLengths.data() + numLitLen, numDist, dynDistTable))
            {
                outUncompressed.clear();
                return false;
            }

            if (!InflateHuffmanBlock(reader, dynLitLenTable, dynDistTable, outUncompressed, maxOutputBytes))
            {
                outUncompressed.clear();
                return false;
            }
        }
        else
        {
            outUncompressed.clear();
            return false;
        }
    } while (bfinal == 0U);

    return true;
}

bool ExtractZipArchive(const fs::path& archivePath, const fs::path& destDir, std::uintmax_t maxTotalBytes)
{
    std::error_code ec;
    const auto fsize = fs::file_size(archivePath, ec);
    constexpr std::uintmax_t kHeaderSlack = 16ULL * 1024ULL * 1024ULL;
    const std::uintmax_t maxFileBytes = (maxTotalBytes <= std::numeric_limits<std::uintmax_t>::max() - kHeaderSlack)
        ? (maxTotalBytes + kHeaderSlack)
        : std::numeric_limits<std::uintmax_t>::max();
    if (ec || fsize < 22 || fsize > maxFileBytes || fsize > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
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
        if (ReadU32(buf.data() + p) == 0x06054b50U)
        {
            eocdPos = p;
            break;
        }
        if (p == minSearch)
            break;
    }
    if (eocdPos == buf.size())
        return false;

    const std::uint16_t entryCount = ReadU16(buf.data() + eocdPos + 10);
    const std::uint32_t cdSize     = ReadU32(buf.data() + eocdPos + 12);
    const std::uint32_t cdOffset   = ReadU32(buf.data() + eocdPos + 16);
    if (entryCount == 0 || static_cast<std::uint64_t>(cdOffset) + cdSize > eocdPos)
        return false;

    fs::create_directories(destDir, ec);
    if (ec)
        return false;

    std::size_t pos = cdOffset;
    std::uintmax_t extractedTotal = 0;
    for (std::uint16_t i = 0; i < entryCount; ++i)
    {
        if (pos + 46 > eocdPos || ReadU32(buf.data() + pos) != 0x02014b50U)
            return false;
        const std::uint16_t verMadeBy = ReadU16(buf.data() + pos + 4);
        const std::uint16_t cdFlags   = ReadU16(buf.data() + pos + 8);
        const std::uint16_t method    = ReadU16(buf.data() + pos + 10);
        const std::uint32_t crc       = ReadU32(buf.data() + pos + 16);
        const std::uint32_t compSz    = ReadU32(buf.data() + pos + 20);
        const std::uint32_t uncompSz  = ReadU32(buf.data() + pos + 24);
        const std::uint16_t nameLen   = ReadU16(buf.data() + pos + 28);
        const std::uint16_t extraLen  = ReadU16(buf.data() + pos + 30);
        const std::uint16_t commLen   = ReadU16(buf.data() + pos + 32);
        const std::uint32_t extAttr   = ReadU32(buf.data() + pos + 38);
        const std::uint32_t localOff  = ReadU32(buf.data() + pos + 42);

        if (static_cast<std::uint64_t>(pos) + 46ULL + nameLen + extraLen + commLen > eocdPos)
            return false;
        std::string rawName(reinterpret_cast<const char*>(buf.data() + pos + 46), nameLen);
        pos += 46 + nameLen + extraLen + commLen;

        // Reject POSIX symlinks (S_IFLNK = 0xA000 in upper 16 bits of external attributes)
        const std::uint16_t hostOs = verMadeBy >> 8U;
        if ((hostOs == 3U || hostOs == 19U) && ((extAttr >> 16U) & 0xF000U) == 0xA000U)
            return false;

        if (!rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\'))
            continue;
        if ((cdFlags & 0x0001U) != 0 || !IsSafeRelativePath(rawName))
            return false;
        if (method != 0 && method != 8)
            return false;
        if (method == 0 && compSz != uncompSz)
            return false;
        if (uncompSz > maxTotalBytes - extractedTotal)
            return false;

        if (static_cast<std::uint64_t>(localOff) + 30ULL > buf.size() || ReadU32(buf.data() + localOff) != 0x04034b50U)
            return false;
        const std::uint16_t lFlags    = ReadU16(buf.data() + localOff + 6);
        const std::uint16_t lMethod   = ReadU16(buf.data() + localOff + 8);
        const std::uint16_t lNameLen  = ReadU16(buf.data() + localOff + 26);
        const std::uint16_t lExtraLen = ReadU16(buf.data() + localOff + 28);
        if ((lFlags & 0x0001U) != 0 || lMethod != method)
            return false;

        const std::uint64_t dataStart64 = static_cast<std::uint64_t>(localOff) + 30ULL + lNameLen + lExtraLen;
        if (dataStart64 > buf.size())
            return false;
        const std::size_t dataStart = static_cast<std::size_t>(dataStart64);
        if (static_cast<std::uint64_t>(compSz) > buf.size() - dataStart)
            return false;

        const std::uintmax_t remainingBudget = maxTotalBytes - extractedTotal;
        const std::size_t maxEntryOut = (remainingBudget > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(remainingBudget);

        std::string content;
        if (method == 0)
        {
            content.assign(reinterpret_cast<const char*>(buf.data() + dataStart), compSz);
        }
        else
        {
            content.reserve(std::min<std::size_t>(static_cast<std::size_t>(uncompSz), 4U * 1024U * 1024U));
            if (!InflateRawDeflate(buf.data() + dataStart, compSz, content, maxEntryOut))
                return false;
            if (content.size() != static_cast<std::size_t>(uncompSz))
                return false;
        }

        const auto actualCrc = ComputeCrc32(reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
        if (actualCrc != crc)
            return false;

        extractedTotal += content.size();

        std::replace(rawName.begin(), rawName.end(), '\\', '/');
        const fs::path relPath(std::u8string(reinterpret_cast<const char8_t*>(rawName.data()), rawName.size()));
        const fs::path outPath = destDir / relPath;
        if (outPath.has_parent_path())
            fs::create_directories(outPath.parent_path(), ec);
        if (!AtomicWrite(outPath, content))
            return false;
    }
    return true;
}

} // namespace sakura::utils
