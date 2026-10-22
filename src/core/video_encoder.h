#pragma once

// video_encoder.h — 通过 ffmpeg 子进程把逐帧读回的画面编码为视频文件（离线录制使用）
// 帧数据经 SDL_process 的标准输入管道以 rawvideo 形式写入；写入在后台线程进行，
// 渲染线程只负责把读回的 SDL_Surface 放入有界队列，从而与 ffmpeg 编码并行。

#include <SDL3/SDL.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sakura::core
{

struct VideoEncoderSettings
{
    std::string              ffmpegPath = "ffmpeg";
    std::vector<std::string> videoArgs;
    std::string              outputPath;   // UTF-8
    std::string              logPath;      // UTF-8；ffmpeg 标准错误输出重定向目标，空 = 丢弃
    int                      width  = 0;
    int                      height = 0;
    int                      fps    = 60;
};

struct VideoEncoderResult
{
    bool        started       = false;   // ffmpeg 进程是否成功启动
    bool        succeeded     = false;   // 进程退出码为 0，且全部帧写入成功
    int         exitCode      = -1;
    long long   framesWritten = 0;
    std::string commandLine;
    std::string pixelFormat;
    std::string error;
};

class VideoEncoder
{
public:
    VideoEncoder() = default;
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&)            = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // 准备新的编码任务；ffmpeg 在第一帧到达、确定像素格式后才启动
    void Open(VideoEncoderSettings settings);

    // 提交一帧并接管 surface 的所有权；编码器已失败（启动失败 / 管道断开）时返回 false
    bool SubmitFrame(SDL_Surface* surface);

    // 关闭标准输入，等待 ffmpeg 完成封装并退出；等待期间周期性调用 idle（可为空）
    VideoEncoderResult Finish(const std::function<void()>& idle = {});

    bool        IsOpen() const    { return m_open; }
    bool        HasFailed() const { return m_failed.load(); }
    std::string GetError() const;

private:
    bool Launch(const std::string& pixelFormat);
    void WriterLoop();
    bool WriteSurface(const SDL_Surface* surface);
    bool WriteBytes(const Uint8* data, size_t size);
    void SetError(const std::string& message);
    void ReleaseQueue();

    static constexpr size_t MAX_QUEUED_FRAMES = 3;

    VideoEncoderSettings     m_settings;
    bool                     m_open            = false;
    SDL_Process*             m_process         = nullptr;
    SDL_IOStream*            m_input           = nullptr;
    SDL_PixelFormat          m_sourceFormat    = SDL_PIXELFORMAT_UNKNOWN;
    std::string              m_pixelFormat;
    std::string              m_commandLine;
    long long                m_framesSubmitted = 0;
    std::atomic<long long>   m_framesWritten{ 0 };

    std::thread              m_writer;
    mutable std::mutex       m_mutex;
    std::condition_variable  m_queueChanged;
    std::deque<SDL_Surface*> m_queue;
    bool                     m_closing    = false;
    bool                     m_writerDone = false;
    std::atomic<bool>        m_failed{ false };
    std::string              m_error;
};

} // namespace sakura::core
