#include "audio/MusicPlayer.h"
#include "core/Common.h"      // using Microsoft::WRL::ComPtr
#include "app/Cloud.h"        // §5 带鉴权下载媒体字节
#include "net/Http.h"         // net::Response
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <cwctype>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "xaudio2.lib")

namespace lj {

// ---- 批次 B：XAudio2 回调（仅 OnStreamEnd 关心：一首自然播完）----
// 注意：回调在 XAudio2 音频线程触发，只碰 atomic，不做任何重活。
namespace {
class VoiceCb : public IXAudio2VoiceCallback
{
public:
    std::atomic<bool>* ended = nullptr;
    void OnStreamEnd() override { if (ended) ended->store(true, std::memory_order_release); }
    void OnVoiceProcessingPassStart(UINT32) override {}
    void OnVoiceProcessingPassEnd() override {}
    void OnBufferStart(void*) override {}
    void OnBufferEnd(void*) override {}
    void OnLoopEnd(void*) override {}
    void OnVoiceError(void*, HRESULT) override {}
};
VoiceCb s_voiceCb;
} // namespace

MusicPlayer& MusicPlayer::Instance()
{
    static MusicPlayer s;
    s_voiceCb.ended = &s.m_ended;   // 单例初始化后接线（幂等）
    return s;
}

MusicPlayer::~MusicPlayer()
{
    Stop();
}

std::wstring MusicPlayer::GetTitle() const
{
    std::lock_guard<std::mutex> lk(m_titleMu);
    return m_title;
}

void MusicPlayer::SetVolume(float v)
{
    m_vol.store((v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v), std::memory_order_release);
    std::lock_guard<std::mutex> lk(m_resMu);
    if (m_voice) m_voice->SetVolume(m_vol.load(std::memory_order_acquire));
}

bool MusicPlayer::IsLocalPath(const std::wstring& id)
{
    if (id.empty()) return false;
    // 含盘符、目录分隔符或常见扩展名 → 视为本地路径
    if (id.find(L':') != std::wstring::npos || id.find(L'\\') != std::wstring::npos) return true;
    size_t dot = id.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = id.substr(dot);
        for (auto& c : ext) c = (wchar_t)std::towlower(c);
        if (ext == L".mp3" || ext == L".wav" || ext == L".ogg" || ext == L".flac" ||
            ext == L".m4a" || ext == L".aac" || ext == L".wma") return true;
    }
    return false;
}

void MusicPlayer::Play(const std::wstring& id, const std::wstring& title)
{
    if (id.empty()) return;
    m_ended.store(false, std::memory_order_release);   // 起播清结束标志
    PlayLocal(IsLocalPath(id) ? id : L"", title);   // local 路径交给 PlayLocal
    if (IsLocalPath(id)) return;
    // 停掉旧线程（join 等它退出），再清状态
    {
        m_stopReq.store(true, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
        m_stopReq.store(false, std::memory_order_release);
    }
    ReleaseVoice();
    {
        std::lock_guard<std::mutex> lk(m_titleMu);
        m_title = title;
    }
    m_state.store((int)State::Loading, std::memory_order_release);
    m_thread = std::thread([this, id, title]() { Worker(id, title, false); });
}

void MusicPlayer::PlayLocal(const std::wstring& path, const std::wstring& title)
{
    if (path.empty()) return;
    m_ended.store(false, std::memory_order_release);   // 起播清结束标志
    // 停掉旧线程（join 等它退出），再清状态
    {
        m_stopReq.store(true, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
        m_stopReq.store(false, std::memory_order_release);
    }
    ReleaseVoice();
    {
        std::lock_guard<std::mutex> lk(m_titleMu);
        m_title = title.empty() ? path.substr(path.find_last_of(L"\\/") + 1) : title;
    }
    m_state.store((int)State::Loading, std::memory_order_release);
    m_thread = std::thread([this, path]() { Worker(path, std::wstring(), true); });
}

void MusicPlayer::Toggle()
{
    State s = GetState();
    if (s == State::Playing) {
        std::lock_guard<std::mutex> lk(m_resMu);
        if (m_voice) m_voice->Stop();
        m_state.store((int)State::Paused, std::memory_order_release);
    } else if (s == State::Paused) {
        std::lock_guard<std::mutex> lk(m_resMu);
        if (m_voice) {
            m_voice->SetVolume(m_vol.load(std::memory_order_acquire));
            m_voice->Start();
        }
        m_state.store((int)State::Playing, std::memory_order_release);
    }
}

void MusicPlayer::Stop()
{
    m_stopReq.store(true, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
    m_stopReq.store(false, std::memory_order_release);
    ReleaseVoice();
    m_ended.store(false, std::memory_order_release);
    m_state.store((int)State::Idle, std::memory_order_release);
}

void MusicPlayer::ReleaseVoice()
{
    std::lock_guard<std::mutex> lk(m_resMu);
    if (m_voice) {
        m_voice->Stop();
        m_voice->DestroyVoice();
        m_voice = nullptr;
    }
    if (m_master) {
        m_master->DestroyVoice();
        m_master = nullptr;
    }
    m_xa.Reset();
    m_pcm.clear();
    m_pcm.shrink_to_fit();
}

void MusicPlayer::Worker(const std::wstring& id, const std::wstring& title, bool local)
{
    // ---- 1) 云端：带鉴权下载；本地：直接指定路径 ----
    net::Response r;
    std::wstring localPath;
    if (local) {
        localPath = id;
    } else {
        r = Cloud::Instance().GetMediaFile(net::ToUtf8(id));
        if (m_stopReq.load(std::memory_order_acquire)) return;
        if (!r.Ok() || r.body.empty()) {
            m_state.store((int)State::Error, std::memory_order_release);
            return;
        }
    }

    // ---- 2) MF 解码 → PCM ----
    std::vector<BYTE> pcm;
    UINT32 channels = 0, rate = 0, bits = 0;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (SUCCEEDED(hr)) {
        ComPtr<IMFSourceReader> reader;
        if (local) {
            hr = MFCreateSourceReaderFromURL(localPath.c_str(), nullptr, &reader);
        } else {
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, r.body.size());
            if (hg) {
                void* p = GlobalLock(hg);
                if (p) {
                    memcpy(p, r.body.data(), r.body.size());
                    GlobalUnlock(hg);
                }
                IStream* st = nullptr;
                // fDeleteOnRelease=FALSE：我们自己 GlobalFree；TRUE 会导致 stream release 后二次释放
                hr = CreateStreamOnHGlobal(hg, FALSE, &st);
                if (SUCCEEDED(hr) && st) {
                    ComPtr<IMFByteStream> bs;
                    hr = MFCreateMFByteStreamOnStream(st, &bs);
                    if (SUCCEEDED(hr) && bs) {
                        hr = MFCreateSourceReaderFromByteStream(bs.Get(), nullptr, &reader);
                    }
                    st->Release();
                }
                GlobalFree(hg);
            } else {
                hr = E_OUTOFMEMORY;
            }
        }

        if (SUCCEEDED(hr) && reader) {
            ComPtr<IMFMediaType> mt;
            hr = MFCreateMediaType(&mt);
            if (SUCCEEDED(hr)) {
                mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt.Get());
            }
            if (SUCCEEDED(hr)) {
                ComPtr<IMFMediaType> out;
                hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &out);
                if (SUCCEEDED(hr) && out) {
                    channels = MFGetAttributeUINT32(out.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
                    rate = MFGetAttributeUINT32(out.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
                    bits = MFGetAttributeUINT32(out.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0);
                }
                for (;;) {
                    if (m_stopReq.load(std::memory_order_acquire)) break;
                    DWORD flags = 0;
                    ComPtr<IMFSample> sample;
                    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr,
                                            &flags, nullptr, &sample);
                    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || !sample) break;
                    ComPtr<IMFMediaBuffer> mb;
                    hr = sample->ConvertToContiguousBuffer(&mb);
                    if (FAILED(hr)) break;
                    BYTE* data = nullptr; DWORD cb = 0;
                    hr = mb->Lock(&data, nullptr, &cb);
                    if (SUCCEEDED(hr)) {
                        size_t old = pcm.size();
                        pcm.resize(old + cb);
                        memcpy(pcm.data() + old, data, cb);
                        mb->Unlock();
                    }
                }
            }
        }
        MFShutdown();
    }

    if (m_stopReq.load(std::memory_order_acquire)) return;
    if (FAILED(hr) || pcm.empty() || channels == 0 || rate == 0 || bits == 0) {
        m_state.store((int)State::Error, std::memory_order_release);
        return;
    }

    // ---- 3) XAudio2 播放 ----
    {
        std::lock_guard<std::mutex> lk(m_resMu);
        if (m_stopReq.load(std::memory_order_acquire)) return;
        HRESULT xr = XAudio2Create(&m_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(xr)) { m_state.store((int)State::Error, std::memory_order_release); return; }
        xr = m_xa->CreateMasteringVoice(&m_master);
        if (FAILED(xr)) { m_state.store((int)State::Error, std::memory_order_release); return; }

        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = (WORD)channels;
        wf.nSamplesPerSec = rate;
        wf.wBitsPerSample = (WORD)bits;
        wf.nBlockAlign = (WORD)(channels * bits / 8);
        wf.nAvgBytesPerSec = rate * wf.nBlockAlign;

        xr = m_xa->CreateSourceVoice(&m_voice, &wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &s_voiceCb);
        if (FAILED(xr)) { m_state.store((int)State::Error, std::memory_order_release); return; }

        m_pcm = std::move(pcm);   // 缓冲必须存活至 voice 销毁
        XAUDIO2_BUFFER ab{};
        ab.AudioBytes = (UINT32)m_pcm.size();
        ab.pAudioData = m_pcm.data();
        // 批次 B：不再单曲无限循环——播完触发 OnStreamEnd → Pump 自动连播下一首
        ab.LoopCount = 0;
        xr = m_voice->SubmitSourceBuffer(&ab);
        if (FAILED(xr)) { m_state.store((int)State::Error, std::memory_order_release); return; }

        m_voice->SetVolume(m_vol.load(std::memory_order_acquire));
        m_voice->Start();
        m_state.store((int)State::Playing, std::memory_order_release);
    }
}

// ============================================================
//  批次 B：播放列表 / 连播（全部 UI 线程调用；m_ended 只在音频线程置位）
// ============================================================
void MusicPlayer::SetPlaylist(std::vector<Track> pl)
{
    std::lock_guard<std::mutex> lk(m_plMu);
    m_playlist = std::move(pl);
    if (m_index >= m_playlist.size()) m_index = 0;
}

void MusicPlayer::SetPlaylistAndPlay(std::vector<Track> pl, size_t index)
{
    Track t;
    {
        std::lock_guard<std::mutex> lk(m_plMu);
        if (pl.empty()) { m_playlist.clear(); m_index = 0; return; }
        m_playlist = std::move(pl);
        m_index = (index < m_playlist.size()) ? index : 0;
        t = m_playlist[m_index];
    }
    Play(t.id, t.title);
}

void MusicPlayer::PlayIndex(size_t index)
{
    Track t;
    {
        std::lock_guard<std::mutex> lk(m_plMu);
        if (index >= m_playlist.size()) return;
        m_index = index;
        t = m_playlist[index];
    }
    Play(t.id, t.title);
}

void MusicPlayer::Next()
{
    Track t;
    {
        std::lock_guard<std::mutex> lk(m_plMu);
        if (m_playlist.empty()) return;
        m_index = (m_index + 1) % m_playlist.size();
        t = m_playlist[m_index];
    }
    Play(t.id, t.title);
}

void MusicPlayer::Prev()
{
    Track t;
    {
        std::lock_guard<std::mutex> lk(m_plMu);
        if (m_playlist.empty()) return;
        m_index = (m_index == 0) ? m_playlist.size() - 1 : m_index - 1;
        t = m_playlist[m_index];
    }
    Play(t.id, t.title);
}

size_t MusicPlayer::Index() const
{
    std::lock_guard<std::mutex> lk(m_plMu);
    return m_index;
}

size_t MusicPlayer::Count() const
{
    std::lock_guard<std::mutex> lk(m_plMu);
    return m_playlist.size();
}

std::vector<MusicPlayer::Track> MusicPlayer::Playlist() const
{
    std::lock_guard<std::mutex> lk(m_plMu);
    return m_playlist;
}

void MusicPlayer::Pump()
{
    // 自然播完 → 自动连播下一首（回卷）。手动 Stop/切歌会把 ended 清回 false。
    if (!m_ended.exchange(false, std::memory_order_acq_rel)) return;
    if (!m_autoNext.load(std::memory_order_acquire)) return;
    Track t;
    {
        std::lock_guard<std::mutex> lk(m_plMu);
        if (m_playlist.empty()) return;
        m_index = (m_index + 1) % m_playlist.size();
        t = m_playlist[m_index];
    }
    Play(t.id, t.title);
}

} // namespace lj
