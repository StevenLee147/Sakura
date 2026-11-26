#include "core/auth_store.h"

#include "core/paths.h"
#include "utils/file_io.h"
#include "utils/logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

namespace sakura::core
{

namespace
{

constexpr std::string_view kDefaultServerUrl = "https://steven-lee.tech:233";
constexpr std::string_view kAuthEntropy      = "Sakura::AuthStore::v2.1";
constexpr std::string_view kDpapiPrefix      = "dpapi:";
constexpr std::string_view kObfPrefix        = "obf:";
constexpr char kBase64Alphabet[]             = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::uint32_t Rotr32(std::uint32_t value, unsigned int shift) noexcept
{
    return (value >> shift) | (value << (32u - shift));
}

std::array<std::uint8_t, 32> Sha256(const std::uint8_t* data, std::size_t len)
{
    static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90beffafu, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    std::array<std::uint32_t, 8> state = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    const std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8ull;
    const std::size_t   rem    = (len + 1u) % 64u;
    const std::size_t   padZeros = (rem <= 56u) ? (56u - rem) : (120u - rem);
    std::vector<std::uint8_t> msg(len + 1u + padZeros + 8u, 0u);
    if (len > 0 && data != nullptr)
    {
        std::memcpy(msg.data(), data, len);
    }
    msg[len] = 0x80u;
    for (std::size_t i = 0; i < 8u; ++i)
    {
        msg[msg.size() - 8u + i] = static_cast<std::uint8_t>((bitLen >> ((7u - i) * 8u)) & 0xFFu);
    }

    std::array<std::uint32_t, 64> w{};
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64u)
    {
        for (std::size_t i = 0; i < 16u; ++i)
        {
            const std::size_t idx = chunk + i * 4u;
            w[i] = (static_cast<std::uint32_t>(msg[idx]) << 24u)
                 | (static_cast<std::uint32_t>(msg[idx + 1u]) << 16u)
                 | (static_cast<std::uint32_t>(msg[idx + 2u]) << 8u)
                 | static_cast<std::uint32_t>(msg[idx + 3u]);
        }
        for (std::size_t i = 16u; i < 64u; ++i)
        {
            const std::uint32_t s0 = Rotr32(w[i - 15u], 7u) ^ Rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
            const std::uint32_t s1 = Rotr32(w[i - 2u], 17u) ^ Rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
            w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t i = 0; i < 64u; ++i)
        {
            const std::uint32_t sum1 = Rotr32(e, 6u) ^ Rotr32(e, 11u) ^ Rotr32(e, 25u);
            const std::uint32_t ch   = (e & f) ^ ((~e) & g);
            const std::uint32_t t1   = h + sum1 + ch + kRoundConstants[i] + w[i];
            const std::uint32_t sum0 = Rotr32(a, 2u) ^ Rotr32(a, 13u) ^ Rotr32(a, 22u);
            const std::uint32_t maj  = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2   = sum0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8u; ++i)
    {
        digest[i * 4u + 0u] = static_cast<std::uint8_t>((state[i] >> 24u) & 0xFFu);
        digest[i * 4u + 1u] = static_cast<std::uint8_t>((state[i] >> 16u) & 0xFFu);
        digest[i * 4u + 2u] = static_cast<std::uint8_t>((state[i] >> 8u) & 0xFFu);
        digest[i * 4u + 3u] = static_cast<std::uint8_t>(state[i] & 0xFFu);
    }
    return digest;
}

std::string Base64Encode(const std::uint8_t* data, std::size_t len)
{
    if (data == nullptr || len == 0)
    {
        return {};
    }

    std::string out;
    out.reserve(((len + 2u) / 3u) * 4u);

    std::size_t i = 0;
    while (i + 3u <= len)
    {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16u)
                                   | (static_cast<std::uint32_t>(data[i + 1u]) << 8u)
                                   | static_cast<std::uint32_t>(data[i + 2u]);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[triple & 0x3Fu]);
        i += 3u;
    }

    const std::size_t rem = len - i;
    if (rem == 1u)
    {
        const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16u;
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (rem == 2u)
    {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16u)
                                   | (static_cast<std::uint32_t>(data[i + 1u]) << 8u);
        out.push_back(kBase64Alphabet[(triple >> 18u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 12u) & 0x3Fu]);
        out.push_back(kBase64Alphabet[(triple >> 6u) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

int DecodeBase64Char(unsigned char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return static_cast<int>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int>(c - 'a') + 26;
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::optional<std::vector<std::uint8_t>> Base64Decode(std::string_view input)
{
    if (input.empty())
    {
        return std::vector<std::uint8_t>{};
    }
    if ((input.size() % 4u) != 0u)
    {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out;
    out.reserve((input.size() / 4u) * 3u);

    for (std::size_t pos = 0; pos < input.size(); pos += 4u)
    {
        const bool isLastGroup = (pos + 4u == input.size());
        const unsigned char c0 = static_cast<unsigned char>(input[pos]);
        const unsigned char c1 = static_cast<unsigned char>(input[pos + 1u]);
        const unsigned char c2 = static_cast<unsigned char>(input[pos + 2u]);
        const unsigned char c3 = static_cast<unsigned char>(input[pos + 3u]);

        const int v0 = DecodeBase64Char(c0);
        const int v1 = DecodeBase64Char(c1);
        if (v0 < 0 || v1 < 0)
        {
            return std::nullopt;
        }

        if (c2 == '=')
        {
            if (!isLastGroup || c3 != '=' || (v1 & 0x0F) != 0)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
        }
        else if (c3 == '=')
        {
            const int v2 = DecodeBase64Char(c2);
            if (!isLastGroup || v2 < 0 || (v2 & 0x03) != 0)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
            out.push_back(static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)));
        }
        else
        {
            const int v2 = DecodeBase64Char(c2);
            const int v3 = DecodeBase64Char(c3);
            if (v2 < 0 || v3 < 0)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
            out.push_back(static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)));
            out.push_back(static_cast<std::uint8_t>(((v2 & 0x03) << 6) | v3));
        }
    }

    return out;
}

std::uint32_t ComputeObfuscationTag(std::string_view plaintext)
{
    std::string macInput;
    macInput.reserve(kAuthEntropy.size() + 14u + plaintext.size());
    macInput.append(kAuthEntropy);
    macInput.append("|auth-tag-v21|");
    const std::uint64_t len64 = static_cast<std::uint64_t>(plaintext.size());
    for (unsigned int i = 0; i < 8u; ++i)
    {
        macInput.push_back(static_cast<char>((len64 >> (i * 8u)) & 0xFFu));
    }
    macInput.append(plaintext);

    const auto digest = Sha256(reinterpret_cast<const std::uint8_t*>(macInput.data()), macInput.size());
    return static_cast<std::uint32_t>(digest[0])
         | (static_cast<std::uint32_t>(digest[1]) << 8u)
         | (static_cast<std::uint32_t>(digest[2]) << 16u)
         | (static_cast<std::uint32_t>(digest[3]) << 24u);
}

std::array<std::uint8_t, 32> DeriveStreamBlock(std::uint32_t counter)
{
    std::string seed;
    seed.reserve(kAuthEntropy.size() + 16u);
    seed.append(kAuthEntropy);
    seed.append("|xor-stream|");
    seed.push_back(static_cast<char>(counter & 0xFFu));
    seed.push_back(static_cast<char>((counter >> 8u) & 0xFFu));
    seed.push_back(static_cast<char>((counter >> 16u) & 0xFFu));
    seed.push_back(static_cast<char>((counter >> 24u) & 0xFFu));
    return Sha256(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size());
}

std::string ObfuscateSecret(std::string_view plaintext)
{
    if (plaintext.empty())
    {
        return {};
    }

    const std::uint32_t tag      = ComputeObfuscationTag(plaintext);
    const auto          tagBlock = DeriveStreamBlock(0u);

    std::vector<std::uint8_t> payload(4u + plaintext.size());
    payload[0] = static_cast<std::uint8_t>((tag & 0xFFu) ^ tagBlock[0]);
    payload[1] = static_cast<std::uint8_t>(((tag >> 8u) & 0xFFu) ^ tagBlock[1]);
    payload[2] = static_cast<std::uint8_t>(((tag >> 16u) & 0xFFu) ^ tagBlock[2]);
    payload[3] = static_cast<std::uint8_t>(((tag >> 24u) & 0xFFu) ^ tagBlock[3]);

    std::array<std::uint8_t, 32> streamBlock{};
    for (std::size_t i = 0; i < plaintext.size(); ++i)
    {
        if ((i % 32u) == 0u)
        {
            streamBlock = DeriveStreamBlock(static_cast<std::uint32_t>((i / 32u) + 1u));
        }
        const auto plainByte = static_cast<std::uint8_t>(plaintext[i]);
        payload[4u + i]      = static_cast<std::uint8_t>(plainByte ^ streamBlock[i % 32u]);
    }

    return std::string(kObfPrefix) + Base64Encode(payload.data(), payload.size());
}

std::optional<std::string> DeobfuscateSecret(std::string_view encodedPayload)
{
    if (encodedPayload.empty())
    {
        LOG_WARN("AuthStore: empty obfuscated payload");
        return std::nullopt;
    }

    const auto decodedOpt = Base64Decode(encodedPayload);
    if (!decodedOpt.has_value() || decodedOpt->size() < 5u)
    {
        LOG_WARN("AuthStore: invalid or truncated obfuscated secret");
        return std::nullopt;
    }

    const auto& bytes    = *decodedOpt;
    const auto  tagBlock = DeriveStreamBlock(0u);
    const std::uint32_t storedTag =
          static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0] ^ tagBlock[0]))
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1] ^ tagBlock[1])) << 8u)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2] ^ tagBlock[2])) << 16u)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3] ^ tagBlock[3])) << 24u);

    const std::size_t plainLen = bytes.size() - 4u;
    std::string plaintext(plainLen, '\0');
    std::array<std::uint8_t, 32> streamBlock{};
    for (std::size_t i = 0; i < plainLen; ++i)
    {
        if ((i % 32u) == 0u)
        {
            streamBlock = DeriveStreamBlock(static_cast<std::uint32_t>((i / 32u) + 1u));
        }
        plaintext[i] = static_cast<char>(static_cast<std::uint8_t>(bytes[4u + i] ^ streamBlock[i % 32u]));
    }

    const std::uint32_t expectedTag = ComputeObfuscationTag(plaintext);
    if (storedTag != expectedTag)
    {
        LOG_WARN("AuthStore: obfuscated secret failed 32-bit integrity verification");
        return std::nullopt;
    }

    return plaintext;
}

std::string NormalizeServerUrl(std::string url)
{
    while (!url.empty() && (url.front() == ' ' || url.front() == '\t' || url.front() == '\r' || url.front() == '\n'))
    {
        url.erase(url.begin());
    }
    while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\t' || url.back() == '\r' || url.back() == '\n'))
    {
        url.pop_back();
    }
    if (url.empty())
    {
        return std::string(kDefaultServerUrl);
    }
    if (url.find("://") == std::string::npos)
    {
        const bool isLocal = (url.rfind("127.0.0.1", 0) == 0 || url.rfind("localhost", 0) == 0);
        url = (isLocal ? "http://" : "https://") + url;
    }
    return url;
}

std::string JsonStringOr(const nlohmann::json& obj, const char* key, std::string_view fallback = {})
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_string())
    {
        return std::string(fallback);
    }
    return obj[key].get<std::string>();
}

bool JsonBoolOr(const nlohmann::json& obj, const char* key, bool fallback)
{
    if (!obj.is_object() || !obj.contains(key))
    {
        return fallback;
    }
    if (obj[key].is_boolean())
    {
        return obj[key].get<bool>();
    }
    if (obj[key].is_number_integer())
    {
        return obj[key].get<long long>() != 0;
    }
    return fallback;
}

double JsonDoubleOr(const nlohmann::json& obj, const char* key, double fallback)
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_number())
    {
        return fallback;
    }
    return obj[key].get<double>();
}

long long JsonInt64Or(const nlohmann::json& obj, const char* key, long long fallback)
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_number_integer())
    {
        return fallback;
    }
    return obj[key].get<long long>();
}

nlohmann::json SerializeAccountEntry(const SavedAccountEntry& entry)
{
    return nlohmann::json{
        { "user_id",                 entry.userId },
        { "username",                entry.username },
        { "is_guest",                entry.isGuest },
        { "total_pp",                entry.totalPP },
        { "sakura_rating",           entry.sakuraRating },
        { "dan_badge",               entry.danBadge },
        { "server_url",              entry.serverUrl },
        { "encrypted_access_token",  entry.encryptedAccessToken },
        { "encrypted_refresh_token", entry.encryptedRefreshToken },
        { "expires_at",              entry.expiresAt },
        { "last_used_at",            entry.lastUsedAt }
    };
}

SavedAccountEntry DeserializeAccountEntry(const nlohmann::json& obj, std::string_view fallbackServerUrl)
{
    SavedAccountEntry entry;
    entry.userId                = JsonStringOr(obj, "user_id", "");
    entry.username              = JsonStringOr(obj, "username", "");
    entry.isGuest               = JsonBoolOr(obj, "is_guest", true);
    entry.totalPP               = JsonDoubleOr(obj, "total_pp", 0.0);
    entry.sakuraRating          = JsonDoubleOr(obj, "sakura_rating", 0.0);
    entry.danBadge              = JsonStringOr(obj, "dan_badge", "");
    const std::string rawUrl    = JsonStringOr(obj, "server_url", fallbackServerUrl);
    entry.serverUrl             = rawUrl.empty() ? std::string(fallbackServerUrl) : NormalizeServerUrl(rawUrl);
    entry.encryptedAccessToken  = JsonStringOr(obj, "encrypted_access_token", "");
    entry.encryptedRefreshToken = JsonStringOr(obj, "encrypted_refresh_token", "");
    entry.expiresAt             = JsonInt64Or(obj, "expires_at", 0LL);
    entry.lastUsedAt            = JsonInt64Or(obj, "last_used_at", 0LL);
    return entry;
}

} // namespace

AuthStore& AuthStore::GetInstance()
{
    static AuthStore instance;
    return instance;
}

AuthStore::AuthStore(std::filesystem::path customStorePath)
    : m_customStorePath(std::move(customStorePath))
{
}

void AuthStore::SetCustomStorePath(std::filesystem::path customStorePath)
{
    std::lock_guard lock(m_mutex);
    m_customStorePath = std::move(customStorePath);
}

std::filesystem::path AuthStore::ResolveStorePath() const
{
    std::lock_guard lock(m_mutex);
    return ResolveStorePathUnlocked();
}

std::filesystem::path AuthStore::ResolveStorePathUnlocked() const
{
    if (!m_customStorePath.empty())
    {
        return m_customStorePath;
    }
    return std::filesystem::path(Paths::User("config/auth_session.json"));
}

bool AuthStore::HasActiveSessionUnlocked() const
{
    return !m_session.activeUserId.empty()
        && (!m_session.accessToken.empty() || !m_session.refreshToken.empty());
}

bool AuthStore::HasActiveSession() const
{
    std::lock_guard lock(m_mutex);
    return HasActiveSessionUnlocked();
}

AuthSessionData AuthStore::GetSession() const
{
    std::lock_guard lock(m_mutex);
    return m_session;
}

void AuthStore::SetServerUrl(const std::string& serverUrl)
{
    std::lock_guard lock(m_mutex);
    m_session.serverUrl = NormalizeServerUrl(serverUrl);
    SaveUnlocked();
}

std::string AuthStore::GetServerUrl() const
{
    std::lock_guard lock(m_mutex);
    return m_session.serverUrl.empty() ? std::string(kDefaultServerUrl) : m_session.serverUrl;
}

long long AuthStore::NextMonotonicTimestampUnlocked(long long floorHint)
{
    const long long nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    long long maxExisting = m_lastIssuedTimestamp;
    if (m_session.profile.lastUsedAt > maxExisting)
    {
        maxExisting = m_session.profile.lastUsedAt;
    }
    for (const auto& entry : m_session.savedAccounts)
    {
        if (entry.lastUsedAt > maxExisting)
        {
            maxExisting = entry.lastUsedAt;
        }
    }

    long long nextTs = std::max(nowSec, maxExisting + 1LL);
    if (floorHint > nextTs)
    {
        nextTs = floorHint;
    }
    m_lastIssuedTimestamp = nextTs;
    return nextTs;
}

void AuthStore::SortAndTrimSavedAccountsUnlocked()
{
    std::stable_sort(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
        [](const SavedAccountEntry& a, const SavedAccountEntry& b)
        {
            return a.lastUsedAt > b.lastUsedAt;
        });

    if (m_session.savedAccounts.size() > MAX_SAVED_ACCOUNTS)
    {
        m_session.savedAccounts.resize(MAX_SAVED_ACCOUNTS);
    }
}

void AuthStore::UpsertActiveSessionUnlocked(const AuthSessionData& session)
{
    const std::string resolvedUserId = !session.activeUserId.empty()
        ? session.activeUserId
        : session.profile.userId;
    const std::string resolvedUrl = NormalizeServerUrl(
        !session.serverUrl.empty()
            ? session.serverUrl
            : (!session.profile.serverUrl.empty() ? session.profile.serverUrl : m_session.serverUrl));

    std::string plainAccess = session.accessToken;
    if (plainAccess.empty() && !session.profile.encryptedAccessToken.empty())
    {
        plainAccess = UnprotectSecret(session.profile.encryptedAccessToken).value_or(std::string{});
    }

    std::string plainRefresh = session.refreshToken;
    if (plainRefresh.empty() && !session.profile.encryptedRefreshToken.empty())
    {
        plainRefresh = UnprotectSecret(session.profile.encryptedRefreshToken).value_or(std::string{});
    }

    const long long resolvedExpiresAt = (session.expiresAt != 0)
        ? session.expiresAt
        : session.profile.expiresAt;

    const std::string encAccess  = ProtectSecret(plainAccess);
    const std::string encRefresh = ProtectSecret(plainRefresh);
    const long long   usedAt     = NextMonotonicTimestampUnlocked(session.profile.lastUsedAt);

    SavedAccountEntry activeEntry = session.profile;
    activeEntry.userId                = resolvedUserId;
    activeEntry.serverUrl             = resolvedUrl;
    activeEntry.encryptedAccessToken  = encAccess;
    activeEntry.encryptedRefreshToken = encRefresh;
    activeEntry.expiresAt             = resolvedExpiresAt;
    activeEntry.lastUsedAt            = usedAt;

    m_session.serverUrl    = resolvedUrl;
    m_session.activeUserId = resolvedUserId;
    m_session.accessToken  = std::move(plainAccess);
    m_session.refreshToken = std::move(plainRefresh);
    m_session.expiresAt    = resolvedExpiresAt;
    m_session.profile      = activeEntry;

    if (!resolvedUserId.empty())
    {
        auto it = std::find_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
            [&](const SavedAccountEntry& existing)
            {
                return existing.userId == resolvedUserId;
            });
        if (it != m_session.savedAccounts.end())
        {
            *it = activeEntry;
        }
        else
        {
            m_session.savedAccounts.push_back(activeEntry);
        }
        SortAndTrimSavedAccountsUnlocked();
    }
}

void AuthStore::UpsertActiveSession(const AuthSessionData& session)
{
    std::lock_guard lock(m_mutex);
    UpsertActiveSessionUnlocked(session);
    SaveUnlocked();
}

void AuthStore::UpdateProfileMetadata(double totalPP,
                                      double sakuraRating,
                                      const std::string& danBadge,
                                      const std::string& username,
                                      std::optional<bool> isGuest)
{
    std::lock_guard lock(m_mutex);
    m_session.profile.totalPP      = totalPP;
    m_session.profile.sakuraRating = sakuraRating;
    m_session.profile.danBadge     = danBadge;
    if (!username.empty())
    {
        m_session.profile.username = username;
    }
    if (isGuest.has_value())
    {
        m_session.profile.isGuest = *isGuest;
    }

    const std::string targetUserId = !m_session.activeUserId.empty()
        ? m_session.activeUserId
        : m_session.profile.userId;
    if (!targetUserId.empty())
    {
        for (auto& entry : m_session.savedAccounts)
        {
            if (entry.userId == targetUserId)
            {
                entry.totalPP      = totalPP;
                entry.sakuraRating = sakuraRating;
                entry.danBadge     = danBadge;
                if (!username.empty())
                {
                    entry.username = username;
                }
                if (isGuest.has_value())
                {
                    entry.isGuest = *isGuest;
                }
                break;
            }
        }
    }

    SaveUnlocked();
}

void AuthStore::ClearActiveTokensKeepIdentity()
{
    std::lock_guard lock(m_mutex);
    m_session.accessToken.clear();
    m_session.refreshToken.clear();
    m_session.expiresAt = 0;
    m_session.profile.encryptedAccessToken.clear();
    m_session.profile.encryptedRefreshToken.clear();
    m_session.profile.expiresAt = 0;

    const std::string targetUserId = !m_session.activeUserId.empty()
        ? m_session.activeUserId
        : m_session.profile.userId;
    if (!targetUserId.empty())
    {
        for (auto& entry : m_session.savedAccounts)
        {
            if (entry.userId == targetUserId)
            {
                entry.encryptedAccessToken.clear();
                entry.encryptedRefreshToken.clear();
                entry.expiresAt = 0;
                break;
            }
        }
    }

    SaveUnlocked();
}

void AuthStore::ClearActiveSession(bool removeFromSavedSlots)
{
    std::lock_guard lock(m_mutex);
    if (removeFromSavedSlots && !m_session.activeUserId.empty())
    {
        const std::string targetId = m_session.activeUserId;
        m_session.savedAccounts.erase(
            std::remove_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
                [&](const SavedAccountEntry& entry) { return entry.userId == targetId; }),
            m_session.savedAccounts.end());
    }

    m_session.activeUserId.clear();
    m_session.accessToken.clear();
    m_session.refreshToken.clear();
    m_session.expiresAt = 0;
    m_session.profile   = SavedAccountEntry{};

    SaveUnlocked();
}

std::vector<SavedAccountEntry> AuthStore::GetSavedAccounts() const
{
    std::lock_guard lock(m_mutex);
    return m_session.savedAccounts;
}

bool AuthStore::SwitchToSavedAccount(const std::string& userId, AuthSessionData* outSession)
{
    std::lock_guard lock(m_mutex);
    if (userId.empty())
    {
        return false;
    }

    auto it = std::find_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
        [&](const SavedAccountEntry& entry)
        {
            return entry.userId == userId;
        });
    if (it == m_session.savedAccounts.end())
    {
        return false;
    }

    bool decryptFailed = false;
    std::string plainAccess;
    if (!it->encryptedAccessToken.empty())
    {
        auto dec = UnprotectSecret(it->encryptedAccessToken);
        if (dec.has_value())
        {
            plainAccess = std::move(*dec);
        }
        else
        {
            decryptFailed = true;
        }
    }

    std::string plainRefresh;
    if (!it->encryptedRefreshToken.empty())
    {
        auto dec = UnprotectSecret(it->encryptedRefreshToken);
        if (dec.has_value())
        {
            plainRefresh = std::move(*dec);
        }
        else
        {
            decryptFailed = true;
        }
    }

    if (decryptFailed && plainAccess.empty() && plainRefresh.empty())
    {
        m_session.activeUserId = it->userId;
        m_session.accessToken.clear();
        m_session.refreshToken.clear();
        m_session.expiresAt    = 0;
        if (!it->serverUrl.empty())
        {
            m_session.serverUrl = NormalizeServerUrl(it->serverUrl);
        }
        m_session.profile = *it;
        m_session.profile.encryptedAccessToken.clear();
        m_session.profile.encryptedRefreshToken.clear();
        m_session.profile.expiresAt = 0;
        if (outSession != nullptr)
        {
            *outSession = m_session;
        }
        return false;
    }

    it->lastUsedAt = NextMonotonicTimestampUnlocked();
    if (!it->serverUrl.empty())
    {
        it->serverUrl       = NormalizeServerUrl(it->serverUrl);
        m_session.serverUrl = it->serverUrl;
    }

    const SavedAccountEntry activated = *it;
    SortAndTrimSavedAccountsUnlocked();

    m_session.activeUserId = activated.userId;
    m_session.accessToken  = std::move(plainAccess);
    m_session.refreshToken = std::move(plainRefresh);
    m_session.expiresAt    = activated.expiresAt;
    m_session.profile      = activated;

    SaveUnlocked();

    if (outSession != nullptr)
    {
        *outSession = m_session;
    }
    return true;
}

bool AuthStore::RemoveSavedAccount(const std::string& userId)
{
    std::lock_guard lock(m_mutex);
    if (userId.empty())
    {
        return false;
    }

    const std::size_t before = m_session.savedAccounts.size();
    m_session.savedAccounts.erase(
        std::remove_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
            [&](const SavedAccountEntry& entry) { return entry.userId == userId; }),
        m_session.savedAccounts.end());

    const bool removed = (m_session.savedAccounts.size() < before);
    if (m_session.activeUserId == userId)
    {
        m_session.activeUserId.clear();
        m_session.accessToken.clear();
        m_session.refreshToken.clear();
        m_session.expiresAt = 0;
        m_session.profile   = SavedAccountEntry{};
    }

    if (removed)
    {
        SaveUnlocked();
    }
    return removed;
}

bool AuthStore::Load(Config* legacyConfigToMigrate)
{
    std::lock_guard lock(m_mutex);

    m_session = AuthSessionData{};
    if (legacyConfigToMigrate != nullptr)
    {
        m_session.serverUrl = NormalizeServerUrl(
            legacyConfigToMigrate->Get<std::string>("online.server_url", std::string(kDefaultServerUrl)));
    }

    const std::filesystem::path storePath = ResolveStorePathUnlocked();
    std::error_code ec;
    const bool fileExists = std::filesystem::exists(storePath, ec) && !ec;
    bool parsedExistingOk = true;

    if (fileExists)
    {
        try
        {
            std::ifstream ifs(storePath, std::ios::binary);
            if (!ifs.is_open())
            {
                LOG_ERROR("AuthStore: failed to open auth session store: {}", storePath.generic_string());
                parsedExistingOk = false;
            }
            else
            {
                const nlohmann::json root = nlohmann::json::parse(ifs, nullptr, true, true);
                if (!root.is_object())
                {
                    LOG_WARN("AuthStore: root JSON in {} is not an object", storePath.generic_string());
                    parsedExistingOk = false;
                }
                else
                {
                    m_session.serverUrl = NormalizeServerUrl(
                        JsonStringOr(root, "server_url", m_session.serverUrl));
                    m_session.activeUserId = JsonStringOr(root, "active_user_id", "");
                    m_session.expiresAt    = JsonInt64Or(root, "expires_at", 0LL);

                    const std::string rootEncAccess  = JsonStringOr(root, "encrypted_access_token", "");
                    const std::string rootEncRefresh = JsonStringOr(root, "encrypted_refresh_token", "");

                    if (!rootEncAccess.empty())
                    {
                        m_session.accessToken = UnprotectSecret(rootEncAccess).value_or(std::string{});
                    }
                    if (!rootEncRefresh.empty())
                    {
                        m_session.refreshToken = UnprotectSecret(rootEncRefresh).value_or(std::string{});
                    }

                    if (root.contains("profile") && root["profile"].is_object())
                    {
                        m_session.profile = DeserializeAccountEntry(root["profile"], m_session.serverUrl);
                        if (m_session.profile.lastUsedAt > m_lastIssuedTimestamp)
                        {
                            m_lastIssuedTimestamp = m_session.profile.lastUsedAt;
                        }
                    }

                    if (root.contains("saved_accounts") && root["saved_accounts"].is_array())
                    {
                        for (const auto& item : root["saved_accounts"])
                        {
                            if (!item.is_object())
                            {
                                continue;
                            }
                            SavedAccountEntry entry = DeserializeAccountEntry(item, m_session.serverUrl);
                            if (entry.userId.empty())
                            {
                                continue;
                            }
                            if (entry.lastUsedAt > m_lastIssuedTimestamp)
                            {
                                m_lastIssuedTimestamp = entry.lastUsedAt;
                            }
                            auto dup = std::find_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
                                [&](const SavedAccountEntry& existing) { return existing.userId == entry.userId; });
                            if (dup != m_session.savedAccounts.end())
                            {
                                if (entry.lastUsedAt >= dup->lastUsedAt)
                                {
                                    *dup = std::move(entry);
                                }
                            }
                            else
                            {
                                m_session.savedAccounts.push_back(std::move(entry));
                            }
                        }
                        SortAndTrimSavedAccountsUnlocked();
                    }

                    if (m_session.profile.userId.empty() && !m_session.activeUserId.empty())
                    {
                        auto it = std::find_if(m_session.savedAccounts.begin(), m_session.savedAccounts.end(),
                            [&](const SavedAccountEntry& entry) { return entry.userId == m_session.activeUserId; });
                        if (it != m_session.savedAccounts.end())
                        {
                            m_session.profile = *it;
                        }
                    }

                    if (rootEncAccess.empty() && !m_session.profile.encryptedAccessToken.empty())
                    {
                        m_session.accessToken = UnprotectSecret(m_session.profile.encryptedAccessToken).value_or(std::string{});
                    }
                    if (rootEncRefresh.empty() && !m_session.profile.encryptedRefreshToken.empty())
                    {
                        m_session.refreshToken = UnprotectSecret(m_session.profile.encryptedRefreshToken).value_or(std::string{});
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            (void)e;
            LOG_WARN("AuthStore: failed to parse {}: {}", storePath.generic_string(), e.what());
            m_session        = AuthSessionData{};
            parsedExistingOk = false;
        }
    }

    const bool eligibleForLegacyMigration =
        (legacyConfigToMigrate != nullptr)
        && (!fileExists || (!HasActiveSessionUnlocked() && m_session.savedAccounts.empty()));

    if (eligibleForLegacyMigration)
    {
        const std::string legacyToken  = legacyConfigToMigrate->Get<std::string>("online.auth_token", "");
        const std::string legacyUserId = legacyConfigToMigrate->Get<std::string>("online.user_id", "");
        if (!legacyToken.empty() && !legacyUserId.empty())
        {
            AuthSessionData migrated;
            migrated.serverUrl = NormalizeServerUrl(
                legacyConfigToMigrate->Get<std::string>("online.server_url", std::string(kDefaultServerUrl)));
            migrated.activeUserId         = legacyUserId;
            migrated.accessToken          = legacyToken;
            migrated.refreshToken         = {};
            migrated.expiresAt            = 0;
            migrated.profile.userId       = legacyUserId;
            migrated.profile.username     = legacyConfigToMigrate->Get<std::string>("online.username", "");
            migrated.profile.isGuest      = legacyConfigToMigrate->Get<bool>("online.is_guest", true);
            migrated.profile.totalPP      = legacyConfigToMigrate->Get<double>("online.total_pp", 0.0);
            migrated.profile.sakuraRating = legacyConfigToMigrate->Get<double>("online.sakura_rating", 0.0);
            migrated.profile.danBadge     = legacyConfigToMigrate->Get<std::string>("online.dan_badge", "");
            migrated.profile.serverUrl    = migrated.serverUrl;

            UpsertActiveSessionUnlocked(migrated);
            if (SaveUnlocked())
            {
                legacyConfigToMigrate->Remove("online.auth_token");
                legacyConfigToMigrate->Remove("online.user_id");
                legacyConfigToMigrate->Remove("online.username");
                legacyConfigToMigrate->Remove("online.is_guest");
                legacyConfigToMigrate->Remove("online.total_pp");
                legacyConfigToMigrate->Remove("online.sakura_rating");
                legacyConfigToMigrate->Remove("online.dan_badge");
                legacyConfigToMigrate->SaveForce();
                LOG_INFO("AuthStore: migrated legacy online credentials for user '{}' into auth_session.json",
                         legacyUserId);
                return true;
            }
            return false;
        }
    }

    return fileExists ? parsedExistingOk : true;
}

bool AuthStore::Save() const
{
    std::lock_guard lock(m_mutex);
    return SaveUnlocked();
}

bool AuthStore::SaveUnlocked() const
{
    try
    {
        const std::filesystem::path storePath = ResolveStorePathUnlocked();
        if (storePath.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(storePath.parent_path(), ec);
            if (ec)
            {
                LOG_ERROR("AuthStore: failed to create parent directories for {}: {}",
                          storePath.generic_string(), ec.message());
                return false;
            }
        }

        const std::string encAccess = !m_session.accessToken.empty()
            ? (!m_session.profile.encryptedAccessToken.empty()
                ? m_session.profile.encryptedAccessToken
                : ProtectSecret(m_session.accessToken))
            : std::string{};
        const std::string encRefresh = !m_session.refreshToken.empty()
            ? (!m_session.profile.encryptedRefreshToken.empty()
                ? m_session.profile.encryptedRefreshToken
                : ProtectSecret(m_session.refreshToken))
            : std::string{};

        SavedAccountEntry profileToDisk     = m_session.profile;
        profileToDisk.encryptedAccessToken  = encAccess;
        profileToDisk.encryptedRefreshToken = encRefresh;

        nlohmann::json savedArray = nlohmann::json::array();
        for (const auto& entry : m_session.savedAccounts)
        {
            savedArray.push_back(SerializeAccountEntry(entry));
        }

        const nlohmann::json root = {
            { "version",                 1 },
            { "server_url",              m_session.serverUrl.empty() ? std::string(kDefaultServerUrl) : m_session.serverUrl },
            { "active_user_id",          m_session.activeUserId },
            { "encrypted_access_token",  encAccess },
            { "encrypted_refresh_token", encRefresh },
            { "expires_at",              m_session.expiresAt },
            { "profile",                 SerializeAccountEntry(profileToDisk) },
            { "saved_accounts",          std::move(savedArray) }
        };

        const std::string serialized = root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!sakura::utils::AtomicWrite(storePath, serialized))
        {
            LOG_ERROR("AuthStore: AtomicWrite failed for {}", storePath.generic_string());
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        (void)e;
        LOG_ERROR("AuthStore: Save failed: {}", e.what());
        return false;
    }
}

std::string AuthStore::ProtectSecret(std::string_view plaintext)
{
    if (plaintext.empty())
    {
        return {};
    }

#ifdef _WIN32
    if (plaintext.size() <= static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
    {
        DATA_BLOB inputBlob{};
        inputBlob.cbData = static_cast<DWORD>(plaintext.size());
        inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));

        DATA_BLOB entropyBlob{};
        entropyBlob.cbData = static_cast<DWORD>(kAuthEntropy.size());
        entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kAuthEntropy.data()));

        DATA_BLOB outputBlob{};
        const BOOL ok = CryptProtectData(
            &inputBlob,
            L"SakuraAuthSession",
            &entropyBlob,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &outputBlob);
        if (ok != FALSE)
        {
            std::string protectedOut = std::string(kDpapiPrefix) + Base64Encode(
                reinterpret_cast<const std::uint8_t*>(outputBlob.pbData),
                static_cast<std::size_t>(outputBlob.cbData));
            if (outputBlob.pbData != nullptr)
            {
                LocalFree(outputBlob.pbData);
            }
            return protectedOut;
        }
        LOG_WARN("AuthStore: CryptProtectData failed (err={}), using obfuscation fallback",
                 static_cast<unsigned long>(GetLastError()));
    }
    return ObfuscateSecret(plaintext);
#else
    return ObfuscateSecret(plaintext);
#endif
}

std::optional<std::string> AuthStore::UnprotectSecret(std::string_view protectedText)
{
    if (protectedText.empty())
    {
        return std::string{};
    }

    try
    {
        if (protectedText.starts_with(kDpapiPrefix))
        {
#ifdef _WIN32
            const std::string_view b64 = protectedText.substr(kDpapiPrefix.size());
            if (b64.empty())
            {
                LOG_WARN("AuthStore: empty dpapi payload");
                return std::nullopt;
            }
            auto cipherOpt = Base64Decode(b64);
            if (!cipherOpt.has_value() || cipherOpt->empty()
                || cipherOpt->size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
            {
                LOG_WARN("AuthStore: invalid Base64 in dpapi payload");
                return std::nullopt;
            }

            DATA_BLOB inputBlob{};
            inputBlob.cbData = static_cast<DWORD>(cipherOpt->size());
            inputBlob.pbData = cipherOpt->data();

            DATA_BLOB entropyBlob{};
            entropyBlob.cbData = static_cast<DWORD>(kAuthEntropy.size());
            entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kAuthEntropy.data()));

            DATA_BLOB outputBlob{};
            const BOOL ok = CryptUnprotectData(
                &inputBlob,
                nullptr,
                &entropyBlob,
                nullptr,
                nullptr,
                CRYPTPROTECT_UI_FORBIDDEN,
                &outputBlob);
            if (ok == FALSE)
            {
                LOG_WARN("AuthStore: CryptUnprotectData failed (err={})",
                         static_cast<unsigned long>(GetLastError()));
                return std::nullopt;
            }

            std::string plaintext(
                reinterpret_cast<const char*>(outputBlob.pbData),
                static_cast<std::size_t>(outputBlob.cbData));
            if (outputBlob.pbData != nullptr)
            {
                SecureZeroMemory(outputBlob.pbData, outputBlob.cbData);
                LocalFree(outputBlob.pbData);
            }
            return plaintext;
#else
            LOG_WARN("AuthStore: dpapi payload is unsupported on non-Windows platforms");
            return std::nullopt;
#endif
        }

        if (protectedText.starts_with(kObfPrefix))
        {
            return DeobfuscateSecret(protectedText.substr(kObfPrefix.size()));
        }

        LOG_WARN("AuthStore: unrecognized secret prefix");
        return std::nullopt;
    }
    catch (...)
    {
        LOG_WARN("AuthStore: unexpected exception during UnprotectSecret");
        return std::nullopt;
    }
}

} // namespace sakura::core
