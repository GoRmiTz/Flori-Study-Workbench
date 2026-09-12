#pragma once

// ============================================================
//  背景音乐播放器（#28 + 批次 B 升级：本地优先 / 播放列表 / 连播）
//  链路：Cloud::GetMediaFile(id) 带鉴权下载 MP3 字节
//        → Media Foundation SourceReader 解码为 PCM
//        → XAudio2 SourceVoice 播放
//  全部为 Windows 系统组件（mfplat / mfreadwrite / xaudio2），
//  与窗口化的 windowscodecs 同级，不引入任何第三方依赖。
//
//  批次 B 新增：
//   - Track 播放列表（SetPlaylist / PlayIndex / Next / Prev），
//     曲目 id 支持本地路径或云端媒体 ID；
//   - 单曲不再无限循环（LoopCount=0），VoiceCallback.OnStreamEnd
//     置结束标志，UI 线程每帧 Pump()：自动连播下一首（到末尾回卷）；
//   - 开始专注自动播放由 RoomView 调 PlayIndex/PlayMusic 实现。
//
//  线程模型：
//   - Play() 从 UI 线程调用，启动后台线程（下载+解码+建 voice）；
//   - 状态用 std::atomic<int>，标题用互斥锁，UI 线程随时可读；
//   - Toggle()/Stop() 跨线程调 XAudio2 voice 方法（XAudio2 线程安全）；
//   - 播放列表 mutex 保护（UI 线程读写，回调线程只置 ended 标志）；
//   - 离开自习室必须 Stop()，释放解码缓冲与 voice。
// ============================================================

#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace lj {

class MusicPlayer
{
public:
    enum class State : int { Idle = 0, Loading, Playing, Paused, Error };

    struct Track
    {
        std::wstring id;      // 本地绝对路径 或 云端媒体 ID（med_xxx）
        std::wstring title;   // 显示标题
    };

    static MusicPlayer& Instance();

    // 播放（若已在播放会先停旧的）。title 供 UI 显示。
    // id 支持两种形态：① 云端媒体 ID（如 med_xxx）→ 带鉴权下载；② 本地文件路径 → 直接解码。
    void Play(const std::wstring& id, const std::wstring& title);
    // 显式播放本地文件（MediaView 调用）
    void PlayLocal(const std::wstring& path, const std::wstring& title);
    // 播放中→暂停；暂停→继续；其他状态忽略
    void Toggle();
    // 停止并释放资源（离开自习室时调用）
    void Stop();

    State GetState() const { return (State)m_state.load(std::memory_order_acquire); }
    std::wstring GetTitle() const;      // 锁内拷贝
    void SetVolume(float v);            // 0..1
    float GetVolume() const { return m_vol.load(std::memory_order_acquire); }

    // ---- 批次 B：播放列表 / 连播 ----
    // 设置播放列表（不打断当前播放）。index 不变若仍合法。
    void SetPlaylist(std::vector<Track> pl);
    // 设置列表并立即播放第 index 首（越界取 0；空列表等价 Stop）。
    void SetPlaylistAndPlay(std::vector<Track> pl, size_t index);
    void Next();                        // 下一首（到末尾回卷第一首）
    void Prev();                        // 上一首（到开头回卷最后一首）
    void PlayIndex(size_t index);       // 播放列表中第 index 首
    size_t Index() const;               // 当前曲目下标（无列表返回 0）
    size_t Count() const;               // 列表长度（锁内拷贝值）
    std::vector<Track> Playlist() const;// 锁内拷贝（UI 只读展示）
    void SetAutoNext(bool on) { m_autoNext.store(on, std::memory_order_release); }
    bool AutoNext() const { return m_autoNext.load(std::memory_order_acquire); }
    // UI 每帧调用（必须在 UI 线程）：一首自然播完 → 自动连播下一首。
    // 手动 Stop / 切歌不会触发（ended 标志在起播时清零）。
    void Pump();

private:
    MusicPlayer() = default;
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    void Worker(const std::wstring& id, const std::wstring& title, bool local);
    void ReleaseVoice();                // 停止并销毁当前 voice + 清 PCM
    static bool IsLocalPath(const std::wstring& id);

    std::atomic<int> m_state{ (int)State::Idle };
    std::atomic<bool> m_stopReq{ false };
    std::atomic<float> m_vol{ 1.0f };
    mutable std::mutex m_titleMu;
    std::wstring m_title;

    std::thread m_thread;                          // 下载+解码线程（可 join）
    Microsoft::WRL::ComPtr<IXAudio2> m_xa;         // 引擎（后台线程创建）
    IXAudio2MasteringVoice* m_master = nullptr;    // 主混音 voice
    IXAudio2SourceVoice* m_voice = nullptr;        // 播放 voice
    std::vector<BYTE> m_pcm;                       // PCM 缓冲（必须存活至 voice 销毁）
    mutable std::mutex m_resMu;                    // 保护 voice/pcm 释放

    // 批次 B：播放列表 / 连播
    mutable std::mutex m_plMu;                     // 保护列表与下标
    std::vector<Track> m_playlist;
    size_t m_index = 0;
    std::atomic<bool> m_autoNext{ true };
    std::atomic<bool> m_ended{ false };            // OnStreamEnd 置位，Pump 消费
};

} // namespace lj
