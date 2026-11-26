#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sakura::net
{

struct ParsedUrl
{
    bool        secure = false;
    std::string host;
    uint16_t    port   = 80;
    std::string pathAndQuery = "/";
};

std::optional<ParsedUrl> ParseHttpUrl(std::string_view url);
std::string UrlEncode(std::string_view value);

class CancelToken
{
public:
    void Cancel() noexcept { m_cancelled.store(true, std::memory_order_release); }
    bool IsCancelled() const noexcept { return m_cancelled.load(std::memory_order_acquire); }

private:
    std::atomic<bool> m_cancelled{ false };
};

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int         timeoutMs = 10000;
    std::function<void(std::size_t downloadedBytes, std::size_t totalBytes)> onProgress;
    std::shared_ptr<CancelToken> cancelToken;
};

struct HttpResponse
{
    int         statusCode = 0;
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;
    bool        cancelled  = false;

    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
    bool IsNotModified() const { return statusCode == 304; }
    bool IsNetworkError() const { return statusCode == 0 && !cancelled; }
    std::string GetHeader(std::string_view name) const;
};

class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse Send(const HttpRequest& request) = 0;
};

class MockHttpTransport final : public IHttpTransport
{
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit MockHttpTransport(Handler handler = {});
    void SetHandler(Handler handler);
    HttpResponse Send(const HttpRequest& request) override;

    std::vector<HttpRequest> GetRecordedRequests() const;
    void ClearRecordedRequests();

private:
    mutable std::mutex       m_mutex;
    Handler                  m_handler;
    std::vector<HttpRequest> m_requests;
};

#ifdef _WIN32
class WinHttpTransport final : public IHttpTransport
{
public:
    static constexpr std::size_t MAX_POOLED_ORIGINS = 8;

    WinHttpTransport() = default;
    ~WinHttpTransport() override;

    WinHttpTransport(const WinHttpTransport&) = delete;
    WinHttpTransport& operator=(const WinHttpTransport&) = delete;
    WinHttpTransport(WinHttpTransport&&) = delete;
    WinHttpTransport& operator=(WinHttpTransport&&) = delete;

    HttpResponse Send(const HttpRequest& request) override;

private:
    struct OriginKey
    {
        std::string lowerHost;
        uint16_t    port   = 0;
        bool        secure = false;

        bool operator==(const OriginKey& other) const noexcept = default;
    };

    struct PoolEntry
    {
        OriginKey             key;
        std::shared_ptr<void> connectHandle;
    };

    struct AcquiredConnection
    {
        std::shared_ptr<void> handle;
        bool                  fromPool  = false;
        std::uint32_t         errorCode = 0;
    };

    void* GetOrCreateSession(bool isLoopback);
    AcquiredConnection AcquireConnection(const OriginKey& key,
                                         void* hSession,
                                         const std::wstring& wideHost,
                                         bool forceFresh);
    void EvictConnection(const OriginKey& key, const void* expectedHandle);

    std::mutex             m_poolMutex;
    void*                  m_autoProxySession = nullptr;
    void*                  m_noProxySession   = nullptr;
    std::vector<PoolEntry> m_pool;
};
#endif

std::shared_ptr<IHttpTransport> CreateDefaultHttpTransport();

} // namespace sakura::net
