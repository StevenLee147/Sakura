// video_encoder.cpp — ffmpeg 子进程视频编码器

#include "video_encoder.h"

#include <chrono>
#include <optional>

#include "record_script.h"
#include "utils/logger.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sakura::core
{

namespace
{

constexpr Uint64 FFMPEG_EXIT_TIMEOUT_NS = 600ULL * 1000000000ULL;

#if defined(_WIN32)
// 共享控制台上的 Ctrl+C / 关闭终端：转换为 SDL 退出事件，让录制收尾当前文件并写出 manifest
BOOL WINAPI ConsoleControlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
        return TRUE;
    }
    return FALSE;
}
#endif

// 没有控制台的 GUI 进程启动控制台程序时，Windows 会为 ffmpeg 新建一个控制台窗口并抢走焦点。
// 若 Sakura 由终端启动，先附加到该终端的控制台，让 ffmpeg 继承它而不弹出新窗口。
void AttachParentConsoleOnce()
{
#if defined(_WIN32)
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    if (GetConsoleWindow() == nullptr && AttachConsole(ATTACH_PARENT_PROCESS))
    {
        SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
        LOG_INFO("[VideoEncoder] 已附加到父进程控制台，ffmpeg 不会弹出新窗口");
    }
#endif
}

// SDL 把标准输入管道写端设为非阻塞（PIPE_NOWAIT）；改回阻塞模式，整帧写入由 ffmpeg 自然反压
void MakePipeBlocking(SDL_IOStream* stream)
{
#if defined(_WIN32)
    void* handle = SDL_GetPointerProperty(SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER, nullptr);
    if (!handle) return;
    DWORD mode = PIPE_WAIT;
    if (!SetNamedPipeHandleState(static_cast<HANDLE>(handle), &mode, nullptr, nullptr))
    {
        LOG_WARN("[VideoEncoder] 无法把管道切换为阻塞模式 (error={})，改用轮询写入",
                 static_cast<unsigned long>(GetLastError()));
    }
#else
    (void)stream;
#endif
}

std::optional<std::string> PixelFormatFor(SDL_PixelFormat format)
{
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(format);
    if (!details) return std::nullopt;
    return FfmpegPixelFormatForLayout(details->bytes_per_pixel, details->Rmask, details->Gmask,
                                      details->Bmask, details->Amask);
}

} // namespace

VideoEncoder::~VideoEncoder()
{
    if (m_open) Finish();
}

void VideoEncoder::Open(VideoEncoderSettings settings)
{
    if (m_open) Finish();
    m_settings        = std::move(settings);
    m_open            = true;
    m_process         = nullptr;
    m_input           = nullptr;
    m_sourceFormat    = SDL_PIXELFORMAT_UNKNOWN;
    m_framesSubmitted = 0;
    m_framesWritten   = 0;
    m_pixelFormat.clear();
    m_commandLine.clear();
    {
        std::lock_guard lock(m_mutex);
        m_closing    = false;
        m_writerDone = false;
        m_failed     = false;
        m_error.clear();
    }
}

std::string VideoEncoder::GetError() const
{
    std::lock_guard lock(m_mutex);
    return m_error;
}

void VideoEncoder::SetError(const std::string& message)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_error.empty()) m_error = message;
        m_failed = true;
    }
    m_queueChanged.notify_all();
    LOG_ERROR("[VideoEncoder] {}", message);
}

bool VideoEncoder::Launch(const std::string& pixelFormat)
{
    m_pixelFormat = pixelFormat;
    const auto arguments = BuildFfmpegArguments(m_settings.ffmpegPath, pixelFormat, m_settings.width,
        m_settings.height, m_settings.fps, m_settings.videoArgs, m_settings.outputPath);
    m_commandLine = JoinCommandLine(arguments);

    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);

    SDL_IOStream* log = nullptr;
    if (!m_settings.logPath.empty())
    {
        log = SDL_IOFromFile(m_settings.logPath.c_str(), "wb");
        if (!log) LOG_WARN("[VideoEncoder] 无法创建 ffmpeg 日志 {}: {}", m_settings.logPath, SDL_GetError());
    }

    AttachParentConsoleOnce();
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
    {
        if (log) SDL_CloseIO(log);
        SetError(std::string("SDL_CreateProperties 失败: ") + SDL_GetError());
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (log)
    {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, log);
    }
    else
    {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    }

    m_process = SDL_CreateProcessWithProperties(props);
    const std::string createError = m_process ? std::string() : std::string(SDL_GetError());
    SDL_DestroyProperties(props);
    // 子进程已持有日志句柄的副本，按 SDL 约定在创建后关闭本方的流
    if (log) SDL_CloseIO(log);
    if (!m_process)
    {
        SetError("无法启动 ffmpeg（" + createError + "）: " + m_commandLine);
        return false;
    }

    m_input = SDL_GetProcessInput(m_process);
    if (!m_input)
    {
        const std::string inputError = SDL_GetError();
        SDL_KillProcess(m_process, true);
        SDL_WaitProcess(m_process, true, nullptr);
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
        SetError("无法获取 ffmpeg 标准输入: " + inputError);
        return false;
    }
    MakePipeBlocking(m_input);

    m_writer = std::thread(&VideoEncoder::WriterLoop, this);
    LOG_INFO("[VideoEncoder] ffmpeg 已启动: {}", m_commandLine);
    return true;
}

bool VideoEncoder::SubmitFrame(SDL_Surface* surface)
{
    if (!surface) return false;
    if (!m_open || m_failed.load())
    {
        SDL_DestroySurface(surface);
        return false;
    }
    if (surface->w != m_settings.width || surface->h != m_settings.height)
    {
        SetError("读回帧尺寸 " + std::to_string(surface->w) + "x" + std::to_string(surface->h) + " 与录制分辨率 "
            + std::to_string(m_settings.width) + "x" + std::to_string(m_settings.height) + " 不符");
        SDL_DestroySurface(surface);
        return false;
    }

    if (!m_process)
    {
        // 读回格式能直接对应 ffmpeg packed 格式时零转换写入，否则统一转换为 ARGB8888
        auto pixelFormat = PixelFormatFor(surface->format);
        m_sourceFormat = surface->format;
        if (!pixelFormat)
        {
            m_sourceFormat = SDL_PIXELFORMAT_ARGB8888;
            pixelFormat = PixelFormatFor(m_sourceFormat);
        }
        LOG_INFO("[VideoEncoder] 读回格式 {} → ffmpeg pix_fmt {}{}", SDL_GetPixelFormatName(surface->format),
                 pixelFormat.value_or("?"), m_sourceFormat != surface->format ? "（逐帧转换为 ARGB8888）" : "");
        if (!pixelFormat || !Launch(*pixelFormat))
        {
            if (!pixelFormat) SetError("无法确定 ffmpeg 像素格式");
            SDL_DestroySurface(surface);
            return false;
        }
    }

    if (surface->format != m_sourceFormat)
    {
        SDL_Surface* converted = SDL_ConvertSurface(surface, m_sourceFormat);
        SDL_DestroySurface(surface);
        if (!converted)
        {
            SetError(std::string("SDL_ConvertSurface 失败: ") + SDL_GetError());
            return false;
        }
        surface = converted;
    }

    {
        std::unique_lock lock(m_mutex);
        m_queueChanged.wait(lock, [this] { return m_queue.size() < MAX_QUEUED_FRAMES || m_failed.load(); });
        if (!m_failed.load())
        {
            m_queue.push_back(surface);
            surface = nullptr;
        }
    }
    if (surface)
    {
        SDL_DestroySurface(surface);
        return false;
    }
    m_queueChanged.notify_all();
    ++m_framesSubmitted;
    return true;
}

void VideoEncoder::WriterLoop()
{
    for (;;)
    {
        SDL_Surface* surface = nullptr;
        {
            std::unique_lock lock(m_mutex);
            m_queueChanged.wait(lock, [this] { return !m_queue.empty() || m_closing || m_failed.load(); });
            if (m_failed.load() || m_queue.empty()) break;
            surface = m_queue.front();
            m_queue.pop_front();
        }
        m_queueChanged.notify_all();
        const bool written = WriteSurface(surface);
        SDL_DestroySurface(surface);
        if (!written) break;
        ++m_framesWritten;
    }
    {
        std::lock_guard lock(m_mutex);
        m_writerDone = true;
    }
    m_queueChanged.notify_all();
}

bool VideoEncoder::WriteSurface(const SDL_Surface* surface)
{
    const size_t rowBytes = static_cast<size_t>(surface->w) * static_cast<size_t>(SDL_BYTESPERPIXEL(surface->format));
    const auto* pixels = static_cast<const Uint8*>(surface->pixels);
    if (static_cast<size_t>(surface->pitch) == rowBytes)
        return WriteBytes(pixels, rowBytes * static_cast<size_t>(surface->h));
    for (int row = 0; row < surface->h; ++row)
        if (!WriteBytes(pixels + static_cast<size_t>(row) * static_cast<size_t>(surface->pitch), rowBytes))
            return false;
    return true;
}

bool VideoEncoder::WriteBytes(const Uint8* data, size_t size)
{
    while (size > 0)
    {
        const size_t written = SDL_WriteIO(m_input, data, size);
        if (written > 0)
        {
            data += written;
            size -= written;
            continue;
        }
        if (SDL_GetIOStatus(m_input) != SDL_IO_STATUS_NOT_READY)
        {
            SetError(std::string("写入 ffmpeg 管道失败（ffmpeg 可能已退出，详见日志）: ") + SDL_GetError());
            return false;
        }
        // 非阻塞回退路径：管道已满时确认 ffmpeg 仍在运行再重试
        int exitCode = 0;
        if (SDL_WaitProcess(m_process, false, &exitCode))
        {
            SetError("ffmpeg 已提前退出（退出码 " + std::to_string(exitCode) + "，详见日志）");
            return false;
        }
        SDL_DelayNS(250000);
    }
    return true;
}

void VideoEncoder::ReleaseQueue()
{
    std::lock_guard lock(m_mutex);
    for (auto* surface : m_queue) SDL_DestroySurface(surface);
    m_queue.clear();
}

VideoEncoderResult VideoEncoder::Finish(const std::function<void()>& idle)
{
    VideoEncoderResult result;
    if (!m_open)
    {
        result.error = "编码器未打开";
        return result;
    }
    m_open = false;

    {
        std::lock_guard lock(m_mutex);
        m_closing = true;
    }
    m_queueChanged.notify_all();
    if (m_writer.joinable())
    {
        for (;;)
        {
            {
                std::unique_lock lock(m_mutex);
                if (m_queueChanged.wait_for(lock, std::chrono::milliseconds(50), [this] { return m_writerDone; }))
                    break;
            }
            if (idle) idle();
        }
        m_writer.join();
    }
    ReleaseQueue();

    result.commandLine   = m_commandLine;
    result.pixelFormat   = m_pixelFormat;
    result.framesWritten = m_framesWritten.load();
    result.started       = m_process != nullptr;

    if (m_process)
    {
        // 关闭标准输入即 EOF：ffmpeg 刷新编码器并写入文件尾
        if (m_input)
        {
            SDL_CloseIO(m_input);
            m_input = nullptr;
        }
        int exitCode = -1;
        bool exited = false;
        const Uint64 deadline = SDL_GetTicksNS() + FFMPEG_EXIT_TIMEOUT_NS;
        for (;;)
        {
            exited = SDL_WaitProcess(m_process, false, &exitCode);
            if (exited) break;
            const char* waitError = SDL_GetError();
            if ((waitError && *waitError) || SDL_GetTicksNS() > deadline) break;
            if (idle) idle();
            SDL_Delay(15);
        }
        if (!exited)
        {
            SetError("等待 ffmpeg 退出失败或超时，已强制结束");
            SDL_KillProcess(m_process, true);
            SDL_WaitProcess(m_process, true, &exitCode);
        }
        result.exitCode = exitCode;
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
        if (exitCode != 0 && GetError().empty())
            SetError("ffmpeg 退出码 " + std::to_string(exitCode) + "（详见 ffmpeg 日志）");
    }
    else if (GetError().empty())
    {
        SetError("没有写入任何帧，ffmpeg 未启动");
    }

    result.error = GetError();
    result.succeeded = result.started && !m_failed.load() && result.exitCode == 0
        && m_framesSubmitted > 0 && result.framesWritten == m_framesSubmitted;
    LOG_INFO("[VideoEncoder] 编码结束: {} 帧, 退出码 {}{}", result.framesWritten, result.exitCode,
             result.error.empty() ? "" : "，错误: " + result.error);
    return result;
}

} // namespace sakura::core
