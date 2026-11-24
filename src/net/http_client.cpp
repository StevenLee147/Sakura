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

HttpResponse MockHttpTransport::Send(const HttpRequest& request)
{
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
    return fn(request);
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
    explicit WinHttpHandleGuard(HINTERNET h = nullptr) : handle(h) {}
    ~WinHttpHandleGuard() { if (handle) WinHttpCloseHandle(handle); }
    WinHttpHandleGuard(const WinHttpHandleGuard&) = delete;
    WinHttpHandleGuard& operator=(const WinHttpHandleGuard&) = delete;
    operator HINTERNET() const { return handle; }
};
}

HttpResponse WinHttpTransport::Send(const HttpRequest& request)
{
    HttpResponse response;
    const auto parsed = ParseHttpUrl(request.url);
    if (!parsed)
    {
        response.error = "Invalid HTTP/HTTPS URL: " + request.url;
        return response;
    }

    const std::string lowerHost = ToLowerAscii(parsed->host);
    const bool isLoopback = (lowerHost == "127.0.0.1" || lowerHost == "localhost" || lowerHost == "::1");
    const DWORD accessType = isLoopback ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;

    WinHttpHandleGuard hSession(WinHttpOpen(
        L"Sakura-RhythmGame/1.1",
        accessType,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!hSession)
    {
        response.error = "WinHttpOpen failed: " + std::to_string(GetLastError());
        return response;
    }

    const int timeout = std::max(1000, request.timeoutMs);
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    const std::wstring wideHost = Utf8ToWide(parsed->host);
    WinHttpHandleGuard hConnect(WinHttpConnect(hSession, wideHost.c_str(), parsed->port, 0));
    if (!hConnect)
    {
        response.error = "WinHttpConnect failed: " + std::to_string(GetLastError());
        return response;
    }

    const std::wstring wideMethod = Utf8ToWide(request.method.empty() ? "GET" : request.method);
    const std::wstring widePath   = Utf8ToWide(parsed->pathAndQuery);
    const DWORD flags = parsed->secure ? WINHTTP_FLAG_SECURE : 0;

    WinHttpHandleGuard hRequest(WinHttpOpenRequest(
        hConnect,
        wideMethod.c_str(),
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!hRequest)
    {
        response.error = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        return response;
    }

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

    if (!WinHttpSendRequest(hRequest, headersPtr, headersLen, bodyPtr, bodyLen, bodyLen, 0))
    {
        response.error = "WinHttpSendRequest failed: " + std::to_string(GetLastError());
        return response;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr))
    {
        response.error = "WinHttpReceiveResponse failed: " + std::to_string(GetLastError());
        return response;
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

    std::string outBody;
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail))
        {
            response.error = "WinHttpQueryDataAvailable failed: " + std::to_string(GetLastError());
            break;
        }
        if (avail == 0)
            break;
        const std::size_t offset = outBody.size();
        outBody.resize(offset + avail);
        DWORD downloaded = 0;
        if (!WinHttpReadData(hRequest, outBody.data() + offset, avail, &downloaded))
        {
            response.error = "WinHttpReadData failed: " + std::to_string(GetLastError());
            break;
        }
        outBody.resize(offset + downloaded);
    }
    response.body = std::move(outBody);
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
