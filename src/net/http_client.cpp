#include "net/http_client.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winhttp.h>
#endif

namespace sakura::net
{

namespace
{
std::string ToLowerAscii(std::string_view input)
{
    std::string out(input);
    for (char& ch : out)
    {
        if (static_cast<unsigned char>(ch) < 128)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}
}

std::optional<ParsedUrl> ParseHttpUrl(std::string_view url)
{
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())))
        url.remove_prefix(1);
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back())))
        url.remove_suffix(1);

    ParsedUrl out;
    if (url.starts_with("https://") || url.starts_with("HTTPS://"))
    {
        out.secure = true;
        out.port   = 443;
        url.remove_prefix(8);
    }
    else if (url.starts_with("http://") || url.starts_with("HTTP://"))
    {
        out.secure = false;
        out.port   = 80;
        url.remove_prefix(7);
    }
    else
    {
        return std::nullopt;
    }

    const std::size_t slashPos = url.find('/');
    const std::size_t queryPos = url.find('?');
    std::size_t authorityEnd = std::string_view::npos;
    if (slashPos != std::string_view::npos && queryPos != std::string_view::npos)
        authorityEnd = std::min(slashPos, queryPos);
    else if (slashPos != std::string_view::npos)
        authorityEnd = slashPos;
    else
        authorityEnd = queryPos;

    std::string_view authority = (authorityEnd == std::string_view::npos) ? url : url.substr(0, authorityEnd);
    if (authority.empty())
        return std::nullopt;

    if (authorityEnd == std::string_view::npos)
    {
        out.pathAndQuery = "/";
    }
    else
    {
        std::string_view tail = url.substr(authorityEnd);
        out.pathAndQuery = tail.starts_with("/") ? std::string(tail) : ("/" + std::string(tail));
    }

    if (authority.front() == '[')
    {
        const std::size_t closeBracket = authority.find(']');
        if (closeBracket == std::string_view::npos)
            return std::nullopt;
        out.host = std::string(authority.substr(1, closeBracket - 1));
        if (closeBracket + 1 < authority.size())
        {
            if (authority[closeBracket + 1] != ':')
                return std::nullopt;
            const std::string portStr(authority.substr(closeBracket + 2));
            int portVal = 0;
            try { portVal = std::stoi(portStr); } catch (...) { return std::nullopt; }
            if (portVal <= 0 || portVal > 65535)
                return std::nullopt;
            out.port = static_cast<uint16_t>(portVal);
        }
    }
    else
    {
        const std::size_t colonPos = authority.rfind(':');
        if (colonPos != std::string_view::npos)
        {
            out.host = std::string(authority.substr(0, colonPos));
            const std::string portStr(authority.substr(colonPos + 1));
            int portVal = 0;
            try { portVal = std::stoi(portStr); } catch (...) { return std::nullopt; }
            if (portVal <= 0 || portVal > 65535)
                return std::nullopt;
            out.port = static_cast<uint16_t>(portVal);
        }
        else
        {
            out.host = std::string(authority);
        }
    }

    if (out.host.empty())
        return std::nullopt;
    return out;
}

std::string UrlEncode(std::string_view value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;
    for (unsigned char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped << static_cast<char>(c);
        }
        else
        {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

std::string HttpResponse::GetHeader(std::string_view name) const
{
    const std::string needle = ToLowerAscii(name);
    for (const auto& [k, v] : headers)
    {
        if (ToLowerAscii(k) == needle)
            return v;
    }
    return {};
}

MockHttpTransport::MockHttpTransport(Handler handler)
    : m_handler(std::move(handler))
{
}

void MockHttpTransport::SetHandler(Handler handler)
{
    std::lock_guard lock(m_mutex);
    m_handler = std::move(handler);
}

namespace
{
struct MockStreamCancelled
{
};
}

HttpResponse MockHttpTransport::Send(const HttpRequest& request)
{
    if (request.cancelToken && request.cancelToken->IsCancelled())
    {
        HttpResponse res;
        res.cancelled  = true;
        res.statusCode = 0;
        return res;
    }

    Handler fn;
    {
        std::lock_guard lock(m_mutex);
        m_requests.push_back(request);
        fn = m_handler;
    }
    if (!fn)
    {
        HttpResponse res;
        res.statusCode = 0;
        res.error      = "MockHttpTransport has no handler configured.";
        return res;
    }

    bool progressCalledByHandler = false;
    HttpRequest handlerReq = request;
    if (request.onProgress)
    {
        handlerReq.onProgress = [&request, &progressCalledByHandler](std::size_t downloadedBytes, std::size_t totalBytes) {
            progressCalledByHandler = true;
            if (request.cancelToken && request.cancelToken->IsCancelled())
                throw MockStreamCancelled{};
            request.onProgress(downloadedBytes, totalBytes);
            if (request.cancelToken && request.cancelToken->IsCancelled())
                throw MockStreamCancelled{};
        };
    }

    HttpResponse res;
    try
    {
        res = fn(handlerReq);
    }
    catch (const MockStreamCancelled&)
    {
        res = HttpResponse{};
        res.cancelled  = true;
        res.statusCode = 0;
        return res;
    }

    if (request.cancelToken && request.cancelToken->IsCancelled())
    {
        res.cancelled  = true;
        res.statusCode = 0;
        return res;
    }

    if (!progressCalledByHandler && request.onProgress && res.IsSuccess())
    {
        request.onProgress(res.body.size(), res.body.size());
        if (request.cancelToken && request.cancelToken->IsCancelled())
        {
            res.cancelled  = true;
            res.statusCode = 0;
            return res;
        }
    }
    return res;
}

std::vector<HttpRequest> MockHttpTransport::GetRecordedRequests() const
{
    std::lock_guard lock(m_mutex);
    return m_requests;
}

void MockHttpTransport::ClearRecordedRequests()
{
    std::lock_guard lock(m_mutex);
    m_requests.clear();
}

#ifdef _WIN32
namespace
{
std::wstring Utf8ToWide(std::string_view str)
{
    if (str.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(std::wstring_view wstr)
{
    if (wstr.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

struct WinHttpHandleGuard
{
    HINTERNET handle = nullptr;
    explicit WinHttpHandleGuard(HINTERNET h = nullptr) noexcept : handle(h) {}
    ~WinHttpHandleGuard() { Close(); }
    WinHttpHandleGuard(const WinHttpHandleGuard&) = delete;
    WinHttpHandleGuard& operator=(const WinHttpHandleGuard&) = delete;

    void Close() noexcept
    {
        if (handle)
        {
            WinHttpCloseHandle(handle);
            handle = nullptr;
        }
    }

    operator HINTERNET() const noexcept { return handle; }
};

bool IsRequestCancelled(const HttpRequest& request) noexcept
{
    return request.cancelToken && request.cancelToken->IsCancelled();
}

HttpResponse MakeCancelledResponse()
{
    HttpResponse res;
    res.statusCode = 0;
    res.cancelled  = true;
    return res;
}

bool IsStaleKeepAliveError(DWORD err) noexcept
{
    return err == ERROR_WINHTTP_CONNECTION_ERROR
        || err == ERROR_WINHTTP_RESEND_REQUEST
        || err == ERROR_WINHTTP_CANNOT_CONNECT;
}
}

WinHttpTransport::~WinHttpTransport()
{
    std::vector<PoolEntry> pooledEntries;
    HINTERNET autoSession    = nullptr;
    HINTERNET noProxySession = nullptr;
    {
        std::lock_guard lock(m_poolMutex);
        pooledEntries      = std::move(m_pool);
        autoSession        = static_cast<HINTERNET>(m_autoProxySession);
        noProxySession     = static_cast<HINTERNET>(m_noProxySession);
        m_autoProxySession = nullptr;
        m_noProxySession   = nullptr;
    }

    pooledEntries.clear();

    if (autoSession)
        WinHttpCloseHandle(autoSession);
    if (noProxySession)
        WinHttpCloseHandle(noProxySession);
}

void* WinHttpTransport::GetOrCreateSession(bool isLoopback)
{
    std::lock_guard lock(m_poolMutex);
    void*& slot = isLoopback ? m_noProxySession : m_autoProxySession;
    if (!slot)
    {
        const DWORD accessType = isLoopback
            ? WINHTTP_ACCESS_TYPE_NO_PROXY
            : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        slot = WinHttpOpen(
            L"Sakura-RhythmGame/1.1",
            accessType,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
    }
    return slot;
}

WinHttpTransport::AcquiredConnection WinHttpTransport::AcquireConnection(
    const OriginKey& key,
    void* hSession,
    const std::wstring& wideHost,
    bool forceFresh)
{
    if (!forceFresh)
    {
        std::lock_guard lock(m_poolMutex);
        for (auto it = m_pool.begin(); it != m_pool.end(); ++it)
        {
            if (it->key == key)
            {
                PoolEntry entry = std::move(*it);
                m_pool.erase(it);
                std::shared_ptr<void> handle = entry.connectHandle;
                m_pool.push_back(std::move(entry));
                return AcquiredConnection{ std::move(handle), true, 0 };
            }
        }
    }

    HINTERNET rawConnect = WinHttpConnect(
        static_cast<HINTERNET>(hSession),
        wideHost.c_str(),
        key.port,
        0);
    if (!rawConnect)
    {
        return AcquiredConnection{ nullptr, false, static_cast<std::uint32_t>(GetLastError()) };
    }

    std::shared_ptr<void> newHandle(rawConnect, [](void* h) noexcept {
        if (h)
            WinHttpCloseHandle(static_cast<HINTERNET>(h));
    });

    std::vector<std::shared_ptr<void>> evictedHandles;
    {
        std::lock_guard lock(m_poolMutex);
        for (auto it = m_pool.begin(); it != m_pool.end(); )
        {
            if (it->key == key)
            {
                evictedHandles.push_back(std::move(it->connectHandle));
                it = m_pool.erase(it);
            }
            else
            {
                ++it;
            }
        }

        while (m_pool.size() >= MAX_POOLED_ORIGINS)
        {
            evictedHandles.push_back(std::move(m_pool.front().connectHandle));
            m_pool.erase(m_pool.begin());
        }

        m_pool.push_back(PoolEntry{ key, newHandle });
    }

    return AcquiredConnection{ std::move(newHandle), false, 0 };
}

void WinHttpTransport::EvictConnection(const OriginKey& key, const void* expectedHandle)
{
    std::shared_ptr<void> evicted;
    {
        std::lock_guard lock(m_poolMutex);
        for (auto it = m_pool.begin(); it != m_pool.end(); ++it)
        {
            if (it->key == key && (expectedHandle == nullptr || it->connectHandle.get() == expectedHandle))
            {
                evicted = std::move(it->connectHandle);
                m_pool.erase(it);
                break;
            }
        }
    }
}

HttpResponse WinHttpTransport::Send(const HttpRequest& request)
{
    if (IsRequestCancelled(request))
        return MakeCancelledResponse();

    const auto parsed = ParseHttpUrl(request.url);
    if (!parsed)
    {
        HttpResponse response;
        response.error = "Invalid HTTP/HTTPS URL: " + request.url;
        return response;
    }

    const std::string lowerHost = ToLowerAscii(parsed->host);
    const bool isLoopback = (lowerHost == "127.0.0.1" || lowerHost == "localhost" || lowerHost == "::1");

    HINTERNET hSession = static_cast<HINTERNET>(GetOrCreateSession(isLoopback));
    if (!hSession)
    {
        HttpResponse response;
        response.error = "WinHttpOpen failed: " + std::to_string(GetLastError());
        return response;
    }

    const OriginKey originKey{ lowerHost, parsed->port, parsed->secure };
    const std::string connectHost = (parsed->host.find(':') != std::string::npos && parsed->host.front() != '[')
        ? ("[" + parsed->host + "]")
        : parsed->host;
    const std::wstring wideHost   = Utf8ToWide(connectHost);
    const std::wstring wideMethod = Utf8ToWide(request.method.empty() ? "GET" : request.method);
    const std::wstring widePath   = Utf8ToWide(parsed->pathAndQuery);
    const DWORD openFlags         = parsed->secure ? WINHTTP_FLAG_SECURE : 0;
    const int timeout             = std::max(1000, request.timeoutMs);

    std::wstring headerBlock;
    for (const auto& [k, v] : request.headers)
    {
        headerBlock += Utf8ToWide(k);
        headerBlock += L": ";
        headerBlock += Utf8ToWide(v);
        headerBlock += L"\r\n";
    }

    LPCWSTR headersPtr = headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str();
    DWORD headersLen   = headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size());
    LPVOID bodyPtr     = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
    DWORD bodyLen      = static_cast<DWORD>(request.body.size());

    HttpResponse response;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (IsRequestCancelled(request))
            return MakeCancelledResponse();

        response = HttpResponse{};
        const AcquiredConnection conn = AcquireConnection(originKey, hSession, wideHost, attempt > 0);
        if (!conn.handle)
        {
            response.error = "WinHttpConnect failed: " + std::to_string(conn.errorCode);
            break;
        }

        WinHttpHandleGuard hRequest(WinHttpOpenRequest(
            static_cast<HINTERNET>(conn.handle.get()),
            wideMethod.c_str(),
            widePath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            openFlags));
        if (!hRequest)
        {
            const DWORD err = GetLastError();
            EvictConnection(originKey, conn.handle.get());
            response.error = "WinHttpOpenRequest failed: " + std::to_string(err);
            break;
        }

        WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

#ifdef WINHTTP_OPTION_DECOMPRESSION
        DWORD decompFlags = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompFlags, sizeof(decompFlags));
#endif

        if (IsRequestCancelled(request))
        {
            hRequest.Close();
            return MakeCancelledResponse();
        }

        if (!WinHttpSendRequest(hRequest, headersPtr, headersLen, bodyPtr, bodyLen, bodyLen, 0))
        {
            const DWORD err = GetLastError();
            hRequest.Close();
            if (IsRequestCancelled(request))
                return MakeCancelledResponse();
            if (IsStaleKeepAliveError(err))
            {
                EvictConnection(originKey, conn.handle.get());
                if (conn.fromPool && attempt == 0)
                    continue;
            }
            response.error = "WinHttpSendRequest failed: " + std::to_string(err);
            break;
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr))
        {
            const DWORD err = GetLastError();
            hRequest.Close();
            if (IsRequestCancelled(request))
                return MakeCancelledResponse();
            if (IsStaleKeepAliveError(err))
            {
                EvictConnection(originKey, conn.handle.get());
                if (conn.fromPool && attempt == 0)
                    continue;
            }
            response.error = "WinHttpReceiveResponse failed: " + std::to_string(err);
            break;
        }

        if (IsRequestCancelled(request))
        {
            hRequest.Close();
            return MakeCancelledResponse();
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX))
        {
            response.statusCode = static_cast<int>(statusCode);
        }

        DWORD rawHeaderBytes = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            nullptr, &rawHeaderBytes, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && rawHeaderBytes > sizeof(wchar_t))
        {
            std::wstring rawHeaders(rawHeaderBytes / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                    rawHeaders.data(), &rawHeaderBytes, WINHTTP_NO_HEADER_INDEX))
            {
                const std::string utf8Headers = WideToUtf8(rawHeaders);
                std::istringstream iss(utf8Headers);
                std::string line;
                while (std::getline(iss, line))
                {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    const std::size_t colon = line.find(':');
                    if (colon != std::string::npos && colon > 0)
                    {
                        std::string key = line.substr(0, colon);
                        std::string val = line.substr(colon + 1);
                        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
                        response.headers.emplace_back(std::move(key), std::move(val));
                    }
                }
            }
        }

        if (IsRequestCancelled(request))
        {
            hRequest.Close();
            return MakeCancelledResponse();
        }

        std::size_t expectedTotal = 0;
        {
            const std::string cl = response.GetHeader("Content-Length");
            if (!cl.empty())
            {
                try { expectedTotal = static_cast<std::size_t>(std::stoull(cl)); } catch (...) { expectedTotal = 0; }
            }
        }

        std::string outBody;
        if (expectedTotal > 0 && expectedTotal <= 64ULL * 1024ULL * 1024ULL)
            outBody.reserve(expectedTotal);
        for (;;)
        {
            if (IsRequestCancelled(request))
            {
                hRequest.Close();
                return MakeCancelledResponse();
            }

            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail))
            {
                const DWORD err = GetLastError();
                hRequest.Close();
                if (IsRequestCancelled(request))
                    return MakeCancelledResponse();
                EvictConnection(originKey, conn.handle.get());
                response.statusCode = 0;
                response.error = "WinHttpQueryDataAvailable failed: " + std::to_string(err);
                return response;
            }
            if (avail == 0)
                break;

            if (IsRequestCancelled(request))
            {
                hRequest.Close();
                return MakeCancelledResponse();
            }

            const std::size_t offset = outBody.size();
            outBody.resize(offset + avail);
            DWORD downloaded = 0;
            if (!WinHttpReadData(hRequest, outBody.data() + offset, avail, &downloaded))
            {
                const DWORD err = GetLastError();
                hRequest.Close();
                if (IsRequestCancelled(request))
                    return MakeCancelledResponse();
                EvictConnection(originKey, conn.handle.get());
                response.statusCode = 0;
                response.error = "WinHttpReadData failed: " + std::to_string(err);
                return response;
            }
            outBody.resize(offset + downloaded);

            if (IsRequestCancelled(request))
            {
                hRequest.Close();
                return MakeCancelledResponse();
            }

            if (request.onProgress && downloaded > 0)
            {
                const std::size_t reportedTotal = (expectedTotal > 0)
                    ? std::max(expectedTotal, outBody.size())
                    : 0;
                request.onProgress(outBody.size(), reportedTotal);
                if (IsRequestCancelled(request))
                {
                    hRequest.Close();
                    return MakeCancelledResponse();
                }
            }
        }
        response.body = std::move(outBody);
        return response;
    }

    return response;
}
#endif

std::shared_ptr<IHttpTransport> CreateDefaultHttpTransport()
{
#ifdef _WIN32
    return std::make_shared<WinHttpTransport>();
#else
    return std::make_shared<MockHttpTransport>([](const HttpRequest&) {
        HttpResponse r;
        r.statusCode = 0;
        r.error      = "Native HTTP transport disabled on non-Win32 build.";
        return r;
    });
#endif
}

} // namespace sakura::net
