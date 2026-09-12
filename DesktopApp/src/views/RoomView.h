#pragma once
// ============================================================
//  RoomView.h — 自习室（No.02）
//  移植 WebApp vRoom()：自习室选择 → 专注计时（番茄钟 + 自由计时）
//  + 今日专注统计 + 近 30 条专注记录 + 静默专注覆盖层。
//  在场成员 / 公共聊天已走 §3 实时（Realtime WS，见 net/Realtime.h），
//  背景音乐走本地 MusicPlayer + 全局浮空播放器；服务端不可达时整体降级为
//  纯本地计时，功能不阻塞。（旧注释写的「待接入服务端」已过时，2026-08-11 修正）
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"
#include <mutex>
#include <atomic>
#include <vector>
#include "net/Realtime.h"   // §3 实时网关：JoinRoom / SendChat / SendPresence / On / Off
#include "app/Cloud.h"      // §1/§2 账户与端点；net::ToUtf8 / FromUtf8 经 net/Http.h
#include "core/FocusTracker.h"  // P1-5 #71 番茄钟联动前台学习状态
#include "ui/FieldEdit.h"   // v2 统一输入框（1×1 透明代理 + D3D 自绘，无白块）

namespace lj {

// ---------------- 自习室选择卡 ----------------
class RoomCard : public Widget
{
public:
    std::wstring icon;     // 单个字（公 / 静 / 冲）
    std::wstring name;
    std::wstring desc;
    D2D1_COLOR_F accent{};
    bool accentSet = false;

    void Paint(Canvas& cv) override;
};

// ---------------- 视图 ----------------
class RoomView : public View
{
public:
    const wchar_t* Id() const override { return L"room"; }
    const wchar_t* Title() const override { return L"自 习 室"; }

    // F-D3 全局热键：静默切换番茄钟计时（不切前台视图），返回切换后是否在运行
    bool HotkeyToggleTimer();

    void OnEnter() override;
    void OnLeave() override;       // 离开视图（路由切换）→ 注销实时处理器
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    // 截图自检：--shot --route room --preview 直接铺开白名单弹层
    void DebugForcePreview() override;
    void DebugForceOpen() override;    // 截图自检：强制专注中（覆盖层 + 音乐控制条）

private:
    enum class TimerState { Idle, Running, Paused };

    // 模式切换
    void EnterRoom(const std::wstring& id);
    void LeaveRoom();
    void BuildSelectWidgets();
    void BuildRoomWidgets();
    void LayoutSelect(const D2D1_RECT_F& area);
    void LayoutRoom(const D2D1_RECT_F& area, Canvas& cv);

    // 计时逻辑
    void ToggleStart();
    void ResetTimer();
    void CancelFocus();
    void CompleteFocus();
    void RecomputeStats();

    // ---- #71 专注白名单（前台进程表单）----
    void OpenWhitelist();
    void CloseWhitelist();
    void LayoutWhitelist(const D2D1_RECT_F& area);
    void UpdateWhitelist(float dt, const Input& in);
    void PaintWhitelist(Canvas& cv);
    void WlAddText();                 // 把输入框文本加入名单
    void WlAddCurrentForeground();    // 一键加入当前前台进程
    void WlCommit();                  // 落盘 + 注入 FocusTracker
    void BeginWlEdit();
    void EndWlEdit();

    // ---- #71 专注结束提醒 ----
    void ShowToast(const std::wstring& msg);
    void PaintToast(Canvas& cv);

    // 绘制
    void PaintSelect(Canvas& cv);
    void PaintRoom(Canvas& cv);
    void PaintTimerCard(Canvas& cv);
    void PaintLinkStatus(Canvas& cv, float x, float y, float right);  // P1-5 #71 联动状态条
    void PaintMembersCard(Canvas& cv);
    void PaintArrangement(Canvas& cv);
    void PaintHistory(Canvas& cv);
    void PaintMusic(Canvas& cv);
    void PaintChat(Canvas& cv);
    void PaintFocusOverlay(Canvas& cv);

    std::wstring CurrentArrangement() const;
    int         CurrentArrangementIndex() const;   // 当前时段匹配打卡项下标；无则 -1
    static std::wstring FmtClock(long long epoch);
    static int ParseSlot(const std::wstring& hhmm);
    // 批次 B：本地优先。扫描音乐目录（settings.musicDir 优先，回退
    // assets/media/music 与系统「音乐」库），返回 {路径, 标题} 全部曲目
    static std::vector<std::pair<std::wstring, std::wstring>> ScanLocalMusic();
    static std::wstring MusicDirOrDefault();   // 用户设置目录 → 默认 assets\media\music
    void PickMusicFolder();                    // 「选择文件夹」：IFileDialog 选目录并落盘
    void AutoStartMusic();                     // 开始专注时自动播放（空闲才起播，暂停则继续）

    // ---- 模式 ----
    bool m_inRoom = true;          // 默认进入「公共自习室」
    std::wstring m_room = L"public";
    bool m_selBuilt = false;
    bool m_roomBuilt = false;
    float m_t = 0.0f;

    // ---- 自习室定义（本地，无服务端）----
    struct RoomDef { std::wstring id, icon, name, desc; D2D1_COLOR_F accent; };
    static const RoomDef kRooms[3];

    // ---- 选择屏控件 ----
    RoomCard m_roomCards[3];
    Button   m_selBack;

    // ---- 计时状态 ----
    TimerState m_timer = TimerState::Idle;
    int   m_presetMin = 25;
    float m_remain = 1500.0f;       // 剩余秒
    long long m_focusStart = 0;     // 本次专注开始 epoch

    // ---- P1-5 #71 番茄钟 × 前台学习联动 ----
    bool m_linkFocus = true;        // 是否联动前台学习状态（默认开）
    bool m_focusLost = false;       // 联动开启且检测到离开学习 → 倒计时自动暂停
    std::wstring m_focusApp;        // 最近一次「离开学习」时的前台进程名（用于提示）
    Button m_linkBtn;               // 联动开关（计时卡右上）
    Button m_wlBtn;                 // 白名单入口（计时卡右上）

    // ---- #71 专注白名单弹层（屏幕坐标，模态）----
    bool   m_wlOpen = false;
    Smooth m_wlA{ 0.0f, 0.16f };
    std::vector<std::wstring> m_wlApps;      // UI 副本（小写进程名）
    D2D1_RECT_F m_wlPanel{};
    D2D1_RECT_F m_wlInputRect{};
    D2D1_RECT_F m_wlListRect{};
    std::vector<D2D1_RECT_F> m_wlItemRects;  // 可视项外框（与 m_wlItemIdx 同序）
    std::vector<int>         m_wlItemIdx;    // 可视项 → m_wlApps 下标
    int    m_wlScroll = 0;                   // 列表首项下标
    int    m_wlHover = -1;                   // 悬停项（可视序号，用于高亮删除标记）
    Button m_wlAdd, m_wlAddCur, m_wlReset, m_wlClose;
    std::vector<Widget*> m_wlWidgets;
    std::wstring m_wlHint;                   // 操作反馈（添加成功/重复等）
    float  m_wlHintT = 0.0f;

    // 白名单输入框（v2 统一输入框；面板固定不随滚动，文字/光标/IME 由 D3D 自绘）
    FieldEdit m_wl;
    bool    m_wlEdit = false;
    std::wstring m_wlText;

    // ---- #71 专注结束提醒（应用内提示条）----
    float  m_toastT = 0.0f;
    std::wstring m_toastMsg;

    // ---- 计时控件 ----
    Button m_leaveBtn;              // 更换自习室
    Button m_presetBtns[5];
    Button m_stepperMinus, m_stepperPlus;
    Button m_startBtn;
    Button m_resetBtn;
    Button m_backBtn;

    // ---- 背景音乐（#28）----
    Button m_musicBtn;              // 音乐卡「播放/暂停」按钮
    D2D1_RECT_F m_volRect{};        // 音量滑条区域（note 内右侧）
    bool  m_volDrag = false;        // 正在拖动音量
    D2D1_RECT_F m_prevR{};          // 音乐卡 ⏮（批次 B 连播）
    D2D1_RECT_F m_nextR{};          // 音乐卡 ⏭
    D2D1_RECT_F m_dirR{};           // 「选择文件夹」小按钮
    // 专注记录收起（默认只显示 5 条，可展开全部）
    bool        m_histExpand = false;
    D2D1_RECT_F m_histMoreR{};      // 「展开全部 / 收起」按钮
    // 专注覆盖层音乐控制条（批次 B：需求 3 —— 专注界面可调音量/暂停/切歌）
    D2D1_RECT_F m_ovPrev{};  D2D1_RECT_F m_ovPlay{};  D2D1_RECT_F m_ovNext{};
    D2D1_RECT_F m_ovVol{};          // 覆盖层音量滑条
    bool  m_ovVolDrag = false;

    // ---- 批次 C：专注体系 ----
    std::wstring m_focusItem;       // 自定义当前专注项（空 = 按当前时段自动）
    D2D1_RECT_F m_arrCancelR{};     // 计时卡「取消固定」小按钮
    bool  m_focusFs = false;        // 专注时启动独立屏保（settings.focusFullscreen）
    void CancelFocusItem();         // 取消自定义专注项 → 恢复时段建议
    void ReloadFocusPrefs();        // 从 settings 读 focusItem / focusFullscreen

    // ---- 批次 C：成员悬停资料卡 ----
    std::vector<D2D1_RECT_F> m_memRows;   // 成员行命中区（内容坐标，与 m_members 平行）
    int   m_memHover = -1;                // 当前悬停成员索引
    float m_memHoverT = 0.0f;             // 悬停持续秒数
    void PaintMemTooltip(Canvas& cv);     // 悬停 2s 显示成员资料卡
    void PlayMusic();               // 播放 meta 匹配的曲目（无匹配取第一首）；暂停→继续
    void MusicVolumeFromX(float x); // 按滑条内 x 坐标换算音量并应用

    // ---- 静默覆盖层 ----
    Smooth m_overlayA{ 0.0f, 0.18f };
    Button m_overlayPause;
    Button m_overlayCancel;
    std::vector<Widget*> m_overlayWidgets;

    // ---- 统计 / 历史 ----
    int m_todayMin = 0;
    int m_totalCount = 0;
    std::vector<FocusSession> m_history;   // 最近 30 条

    // ---- 今日打卡项（与打卡页同源，来自用户自定义的 items.json；
    //      用于自习室「当前时段 · 专注建议」与下一节匹配，避免显示旧默认计划）----
    std::vector<CheckItem> m_todayItems;

    // ---- 布局缓存（屏幕/文档坐标）----
    float m_contentTop = 0.0f;
    D2D1_RECT_F m_timerCard{};
    D2D1_RECT_F m_membersCard{};
    D2D1_RECT_F m_histCard{};
    D2D1_RECT_F m_musicCard{};
    D2D1_RECT_F m_chatCard{};
    float m_countdownY = 0.0f;
    float m_presetY = 0.0f;
    float m_stepperY = 0.0f;
    float m_ctrlY = 0.0f;
    float m_arrY = 0.0f;
    float m_statY = 0.0f;
    float m_musicY = 0.0f;
    float m_histY = 0.0f;
    float m_chatY = 0.0f;

    std::vector<Widget*> m_widgets;   // 底层控件（选择屏 / 房间内）

    // ---- §3 云端实时数据 ----
    //  来源（受 m_netMu 保护，由 Realtime 后台线程的下行回调写入）；
    //  UI 线程只在 Update 里拷贝成「快照」后由 Paint 只读，绝不在回调里碰渲染。
    struct NetMember { std::wstring uid, name, status, focusContent; int focusSeconds = 0; long long since = 0; };
    struct NetChat   { std::wstring id, user, uid, text; long long ts = 0; };

    std::mutex  m_netMu;
    std::atomic<bool> m_netDirty{ false };   // 后台收到新帧 → 置位；Update 消费后重排/快照

    std::vector<NetMember> m_srcMembers; bool m_srcHasMembers = false;
    std::vector<NetChat>   m_srcChat;
    std::wstring m_srcTrack;       bool m_srcHasTrack = false;
    // #28 曲目表：{id, title}（music:list 下行），播放时按 title 匹配 meta 或取第一首
    std::vector<std::pair<std::wstring, std::wstring>> m_srcMusic; bool m_srcHasMusic = false;

    // UI 线程快照（Paint 只读）
    std::vector<NetMember> m_members;
    std::vector<NetChat>   m_chat;
    std::wstring m_track;          bool m_hasTrack = false;
    std::vector<std::pair<std::wstring, std::wstring>> m_music; bool m_hasMusic = false;
    bool m_netLive = false;        // 快照时是否连上服务端（Realtime::Connected）

    // 聊天输入：v2 统一输入框（1×1 透明代理收键盘 + IME，文字/光标/组合串全由 D3D 自绘）。
    bool m_chatEdit = false;
    std::wstring m_compose;
    Button m_sendBtn;
    D2D1_RECT_F m_chatInputRect{};
    D2D1_RECT_F m_chatSendRect{};
    Canvas* m_cvCached = nullptr;       // Update 内 HandleMouse 需要测量
    FieldEdit m_chatBox;                // 聊天输入框（避开消息列表 m_chat 的名字）
    void BeginChatEdit();
    void EndChatEdit(bool send);        // true=发送并清空；false=退出编辑但保留文本

    // 下行处理器（Realtime 后台线程回调，只写受锁缓冲 + 置 m_netDirty）
    void OnNetPresence(const lj::json::JVal& m);
    void OnNetChatMsg(const lj::json::JVal& m);
    void OnNetChatHistory(const lj::json::JVal& m);
    void OnNetMusicMeta(const lj::json::JVal& m);
    void OnNetMusicList(const lj::json::JVal& m);
    void OnNetAuthOk(const lj::json::JVal& m);    // 连上/重连成功 → 重新进房 + 发在场 + 拉音乐
    void SendMyPresence();                         // 把当前专注状态上行到房间
    void SnapshotNet();                            // 来源 → 快照（Update 内调用）
};

} // namespace lj
