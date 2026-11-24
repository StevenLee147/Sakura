#pragma once

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

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int         timeoutMs = 10000;
};

struct HttpResponse
{
    int         statusCode = 0;
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;

    bool IsSuccess() const { return statusCode >= 200 && statusCode < 300; }
    bool IsNetworkError() const { return statusCode == 0; }
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
    HttpResponse Send(const HttpRequest& request) override;
};
#endif

std::shared_ptr<IHttpTransport> CreateDefaultHttpTransport();

} // namespace sakura::net
