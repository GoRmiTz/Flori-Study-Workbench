#pragma once

// ============================================================
//  背景音乐播放器（#28）
//  链路：Cloud::GetMediaFile(id) 带鉴权下载 MP3 字节
//        → Media Foundation SourceReader 解码为 PCM
//        → XAudio2 SourceVoice 播放（无限循环）
//  全部为 Windows 系统组件（mfplat / mfreadwrite / xaudio2），
//  与窗口化的 windowscodecs 同级，不引入任何第三方依赖。
//
//  线程模型：
//   - Play() 从 UI 线程调用，启动后台线程（下载+解码+建 voice）；
//   - 状态用 std::atomic<int>，标题用互斥锁，UI 线程随时可读；
//   - Toggle()/Stop() 跨线程调 XAudio2 voice 方法（XAudio2 线程安全）；
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
};

} // namespace lj
