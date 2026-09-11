#include "views/RoomView.h"
#include "app/Store.h"
#include "core/Hwnd.h"      // AppHwnd / AppDpi / lj::SetEditBackdrop / AppPalette
#include "core/TrayIcon.h"  // #71 专注结束系统气泡提醒
#include "core/FloatLayer.h" // F-D2 浮层读取自习室在线人数
#include "audio/MusicPlayer.h"
#include "ui/Glyphs.h"      // #49 矢量播放/暂停图标（替代 ⏸ 表情蓝方块）
#include "ui/Layout.h"
#include <windows.h>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <string>
#include <cwctype>

namespace lj {

// 本地自习室定义（无服务端，纯本地「房间」概念）
const RoomView::RoomDef RoomView::kRooms[3] = {
    { L"public", L"公", L"公共自习室", L"人数不限，所有人共处一室", Hex(0x6B2A35) },
    { L"silent", L"静", L"静音自习室", L"禁言 · 只计时不闲聊",       Hex(0x9c7326) },
    { L"sprint", L"冲", L"冲刺自习室", L"考前 30 天高强度节奏",       Hex(0x3b6448) },
};

// ============================================================
//  RoomCard — 选择屏房间卡
// ============================================================
void RoomCard::Paint(Canvas& cv)
{
    if (!visible) return;
    const auto& pal = cv.Pal();
    float ev = enter.running ? enter.Value() : 1.0f;
    if (ev <= 0.001f) return;
    float h = m_hover.value;
    float p = m_press.value;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ev) * 14.0f));
    cv.PushOpacity(ev);

    D2D1_RECT_F r = bounds;
    r.top += p; r.bottom += p;

    cv.FillRoundRect(r, shape::kEdge, MixColor(pal.paperHi, pal.rule, h * 0.5f));
    cv.StrokeRoundRect(r, shape::kEdge, MixColor(pal.rule, accentSet ? accent : pal.seal, h * 0.7f),
                       h > 0.02f ? shape::kStroke : shape::kHair);
    if (h > 0.01f)
        cv.CornerTicks({ r.left - 3.0f, r.top - 3.0f, r.right + 3.0f, r.bottom + 3.0f },
                       WithAlpha(accentSet ? accent : pal.seal, h * 0.45f), 9.0f, 1.0f);

    // 图标方块
    float ib = 46.0f;
    D2D1_RECT_F ibox{ r.left + 22.0f, (r.top + r.bottom) * 0.5f - ib / 2.0f,
                      r.left + 22.0f + ib, (r.top + r.bottom) * 0.5f + ib / 2.0f };
    cv.FillRoundRect(ibox, shape::kEdge, WithAlpha(accentSet ? accent : pal.seal, 0.14f));
    cv.StrokeRoundRect(ibox, shape::kEdge, accentSet ? accent : pal.seal, 1.2f);
    TextStyle its; its.role = FontRole::Serif; its.size = 22.0f;
    its.weight = DWRITE_FONT_WEIGHT_BLACK; its.vAlign = VAlign::Middle; its.hAlign = HAlign::Center;
    cv.Text(icon, ibox, its, accentSet ? accent : pal.seal);

    // 名称 + 描述
    float tx = ibox.right + 18.0f;
    TextStyle ns; ns.role = FontRole::Sans; ns.size = 17.0f; ns.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(name, { tx, r.top + 18.0f, r.right - 120.0f, r.top + 44.0f }, ns, pal.ink900);

    TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.5f; ds.vAlign = VAlign::Top;
    cv.Text(desc, { tx, r.top + 46.0f, r.right - 120.0f, r.bottom - 14.0f }, ds, pal.ink500);

    // 进入提示
    TextStyle es; es.role = FontRole::Mono; es.size = 12.0f; es.letterSpacing = 1.5f;
    es.vAlign = VAlign::Middle; es.hAlign = HAlign::Right;
    cv.Text(L"进入 →", { r.right - 120.0f, r.top, r.right - 22.0f, r.bottom }, es,
            h > 0.02f ? (accentSet ? accent : pal.seal) : pal.ink300);

    cv.PopOpacity();
    cv.PopTransform();
}

// ============================================================
//  模式 / 计时逻辑
// ============================================================
void RoomView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_inRoom = true;
    m_room = L"public";
    m_timer = TimerState::Idle;
    m_presetMin = 25;
    m_remain = 1500.0f;
    m_overlayA.Snap(0.0f);
    m_selBuilt = false;
    m_roomBuilt = false;
    // 今日打卡项与打卡页同源（自定义 items.json，无存档回退默认），
    // 供「当前时段 · 专注建议」与下一节匹配使用，不再显示旧默认计划。
    m_todayItems = ItemsForDate(CheckinStore::Instance().LoadItems(), Today());
    RecomputeStats();

    // #71 专注白名单：与 FocusTracker 现行名单对齐（切换账户后名单随账户走）。
    // 播种默认清单只在 FocusTracker::Start() 里做一次；这里对未启用检测的
    // 访客 / 截图模式只读不写，避免无谓地改写账户档案。
    if (FocusTracker::Instance().Running()) {
        FocusTracker::Instance().SetUserApps(FocusTracker::LoadUserAppsFromSettings());
        m_wlApps = FocusTracker::Instance().UserApps();
    } else {
        m_wlApps = CheckinStore::Instance().LoadSettings().focusApps;
    }
    m_wlOpen = false;
    m_wlA.Snap(0.0f);
    m_wlScroll = 0;
    m_wlText.clear();
    m_wlHint.clear();
    m_wlHintT = 0.0f;

    // ---- §3 接入：注册实时下行处理器 + 进房 ----
    // 处理器运行在 Realtime 后台线程，只能写受锁缓冲 + 置 m_netDirty，绝不碰渲染。
    auto rt = &Realtime::Instance();
    rt->On("auth:ok",     [this](const lj::json::JVal& m) { OnNetAuthOk(m); });
    rt->On("presence",    [this](const lj::json::JVal& m) { OnNetPresence(m); });
    rt->On("chat:msg",    [this](const lj::json::JVal& m) { OnNetChatMsg(m); });
    rt->On("chat:history",[this](const lj::json::JVal& m) { OnNetChatHistory(m); });
    rt->On("music:meta",  [this](const lj::json::JVal& m) { OnNetMusicMeta(m); });
    rt->On("music:list",  [this](const lj::json::JVal& m) { OnNetMusicList(m); });
    // 进房 / 拉音乐 / 发在场（若尚未连上，auth:ok 会重做一遍）
    rt->JoinRoom(net::ToUtf8(m_room));
    rt->RequestMusic();
    SendMyPresence();
}

void RoomView::OnLeave()
{
    // 注销本视图注册的下行处理器，避免离开后仍回调已失效的视图；
    // 并显式离房（服务器会广播在场变化）。
    auto rt = &Realtime::Instance();
    rt->Off("auth:ok");
    rt->Off("presence");
    rt->Off("chat:msg");
    rt->Off("chat:history");
    rt->Off("music:meta");
    rt->Off("music:list");
    rt->LeaveRoom();
    g_studyRoomOnline = 0;   // F-D2 浮层：离房即无在线计数
    if (m_chatEditHwnd) ShowWindow(m_chatEditHwnd, SW_HIDE);   // 收起输入代理
    m_chatEdit = false;
    m_compose.clear();
    // #71 白名单弹层随视图关闭，代理窗一并收起，避免残留在其它页面上收键盘
    if (m_wlEditHwnd) ShowWindow(m_wlEditHwnd, SW_HIDE);
    m_wlEdit = false;
    m_wlOpen = false;
    m_wlA.Snap(0.0f);
    // #49 不再于此停止背景音乐：音乐跨页常驻，由右下角浮空播放器的 × 关闭。
}

// ============================================================
//  聊天输入（隐藏 EDIT 代理承载 IME，文字/光标由 D3D 自绘）
// ============================================================
LRESULT CALLBACK RoomView::ChatEditProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    RoomView* self = (RoomView*)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (self) {
        if (msg == WM_KEYDOWN) {
            // 组合中回车/ESC 属于输入法（选字/撤销候选），必须让 IME 先吃掉，不能当成发送
            if (self->m_imeComp.empty()) {
                if (wp == VK_RETURN) { self->EndChatEdit(true); return 0; }   // Enter 发送
                if (wp == VK_ESCAPE) { SetWindowTextW(w, L""); self->EndChatEdit(false); return 0; } // Esc 清空退出
            }
        } else if (msg == WM_SETFOCUS) {
            int len = GetWindowTextLengthW(w);
            SendMessageW(w, EM_SETSEL, (WPARAM)len, (LPARAM)len);   // 焦点时光标置文末
        } else if (msg == WM_KILLFOCUS) {
            // 失焦（点外部/点发送按钮）→ 退出编辑但保留文本，发送按钮再读 m_compose 发送
            self->EndChatEdit(false);
        }
    }
    WNDPROC old = self ? self->m_chatEditOld : nullptr;
    LRESULT r = old ? CallWindowProcW(old, w, msg, wp, lp) : DefWindowProcW(w, msg, wp, lp);

    // ---- P1-2 中文 IME 合成 ----
    // 交给 EDIT 处理后再读 IME 上下文：结果串已由 EDIT 落入文本缓冲，
    // 未提交的组合串仍留在上下文里，取出来交给 D2D 自绘（否则打拼音全程不可见）。
    if (self) {
        if (msg == WM_IME_STARTCOMPOSITION) {
            self->m_imeComp.clear(); self->m_imeCompCaret = 0;
            lj::ImeSetCandidatePos(self->m_chatEditHwnd,
                self->m_imeAnchorX - self->m_chatInputRect.left,
                self->m_chatInputRect.bottom - self->m_chatInputRect.top, AppDpi());
        } else if (msg == WM_IME_COMPOSITION) {
            if (lj::ImeReadComposition(w, self->m_imeComp, self->m_imeCompCaret)) {
                lj::ImeSetCandidatePos(self->m_chatEditHwnd,
                    self->m_imeAnchorX - self->m_chatInputRect.left,
                    self->m_chatInputRect.bottom - self->m_chatInputRect.top, AppDpi());
            }
            if (lp & GCS_RESULTSTR) self->m_imeComp.clear();
        } else if (msg == WM_IME_ENDCOMPOSITION) {
            self->m_imeComp.clear(); self->m_imeCompCaret = 0;
        } else if (msg == WM_IME_SETCONTEXT) {
            lj::ImeSetCandidatePos(self->m_chatEditHwnd,
                self->m_imeAnchorX - self->m_chatInputRect.left,
                self->m_chatInputRect.bottom - self->m_chatInputRect.top, AppDpi());
        }
    }

    // 隐藏代理 EDIT 的原生系统光标：我们用 D3D 自绘光标，否则原生光标会在输入框处闪一下
    if (msg == WM_SETFOCUS || msg == WM_KEYDOWN || msg == WM_CHAR ||
        msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_CHAR)
        HideCaret(w);
    return r;
}

// 代理窗跟随页面滚动。m_chatInputRect 是文档坐标，屏幕位置 = 文档坐标 − ScrollY；
// 不减这一项，页面一滚代理窗就留在原地：鼠标点不中、候选窗也会飘到别处。
void RoomView::SyncChatEditorRect()
{
    if (!m_chatEditHwnd) return;
    float s = (float)AppDpi() / 96.0f;
    int L = (int)(m_chatInputRect.left * s);
    int T = (int)((m_chatInputRect.top - ScrollY()) * s);
    int W = (int)((m_chatInputRect.right - m_chatInputRect.left) * s);
    int H = (int)((m_chatInputRect.bottom - m_chatInputRect.top) * s);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (L == m_chatEditPlaced[0] && T == m_chatEditPlaced[1] &&
        W == m_chatEditPlaced[2] && H == m_chatEditPlaced[3]) return;   // 未变则不动，避免每帧 SetWindowPos
    SetWindowPos(m_chatEditHwnd, nullptr, L, T, W, H, SWP_NOZORDER);
    m_chatEditPlaced[0] = L; m_chatEditPlaced[1] = T;
    m_chatEditPlaced[2] = W; m_chatEditPlaced[3] = H;
}

void RoomView::EnsureChatEditor()
{
    if (m_chatEditHwnd) return;
    HWND parent = AppHwnd();
    if (!parent) return;
    // 不加 WS_EX_TRANSPARENT：编辑态铺成输入框全尺寸收鼠标（点击定位/拖拽框选），
    // 配合 WM_SETREDRAW(FALSE) 不自绘——文字/光标/选区全由 D3D 绘制。
    m_chatEditHwnd = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                                     0, 0, 10, 10, parent, nullptr,
                                     (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
    if (!m_chatEditHwnd) return;
    m_chatEditOld = (WNDPROC)SetWindowLongPtrW(m_chatEditHwnd, GWLP_WNDPROC, (LONG_PTR)ChatEditProc);
    SetWindowLongPtrW(m_chatEditHwnd, GWLP_USERDATA, (LONG_PTR)this);
    m_chatEditFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Microsoft YaHei UI");
    if (m_chatEditFont) SendMessageW(m_chatEditHwnd, WM_SETFONT, (WPARAM)m_chatEditFont, TRUE);
    ShowWindow(m_chatEditHwnd, SW_HIDE);
}

void RoomView::BeginChatEdit()
{
    EnsureChatEditor();
    if (!m_chatEditHwnd) return;
    // 输入框编辑态背板是 paperHi，代理背景与其一致 → 无缝（代理本身 1x1 透明）
    lj::SetEditBackdrop(AppPalette().paperHi);
    SetWindowTextW(m_chatEditHwnd, m_compose.c_str());
    // 编辑框铺满输入框全尺寸（内部处理鼠标点击定位/拖拽框选），
    // WM_SETREDRAW(FALSE) 禁止自绘——可见文字/光标/选区全由 D3D 绘制。
    m_chatEditPlaced[0] = -1;    // 强制重放一次（进编辑态时页面可能已滚动）
    SyncChatEditorRect();
    SendMessageW(m_chatEditHwnd, WM_SETREDRAW, FALSE, 0);
    ShowWindow(m_chatEditHwnd, SW_SHOW);
    SetFocus(m_chatEditHwnd);
    int len = GetWindowTextLengthW(m_chatEditHwnd);
    SendMessageW(m_chatEditHwnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    // IME 组合字号跟随输入框文字（12.5 DIP → 物理像素），否则候选窗字号与界面脱节
    LOGFONTW lf{};
    lf.lfHeight = -(LONG)(12.5f * (float)AppDpi() / 96.0f);
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    if (HIMC himc = ImmGetContext(m_chatEditHwnd)) {
        ImmSetCompositionFontW(himc, &lf);
        ImmReleaseContext(m_chatEditHwnd, himc);
    }
    m_imeComp.clear(); m_imeCompCaret = 0;
    m_imeAnchorX = m_chatInputRect.left + 12.0f;   // 首帧锚点：文字起点（Paint 会立刻校正）
    lj::ImeSetCandidatePos(m_chatEditHwnd,
        m_imeAnchorX - m_chatInputRect.left,
        m_chatInputRect.bottom - m_chatInputRect.top, AppDpi());
}

void RoomView::EndChatEdit(bool send)
{
    if (m_chatEditHwnd) {
        // 退出编辑时若仍在组合中，先取消合成——否则候选窗会遗留在屏幕上
        if (!m_imeComp.empty()) lj::ImeCancelComposition(m_chatEditHwnd);
        m_imeComp.clear(); m_imeCompCaret = 0;
        if (send) {
            std::wstring text = ReadEditBuffer(m_chatEditHwnd);
            if (!text.empty()) Realtime::Instance().SendChat(net::ToUtf8(text));
            m_compose.clear();
        } else {
            m_compose = ReadEditBuffer(m_chatEditHwnd);   // 保留文本：点回来继续编辑/发送
        }
        SendMessageW(m_chatEditHwnd, WM_SETREDRAW, TRUE, 0);   // 恢复自绘能力（隐藏后不再画）
        ShowWindow(m_chatEditHwnd, SW_HIDE);
    }
    m_chatEdit = false;
}

// ============================================================
//  #71 专注白名单（前台进程表单）
//  名单内进程 = 专注（番茄钟继续走），名单外 = 自动暂停；
//  名单为空则回退 FocusTracker 内置智能判定。落盘 settings.json。
// ============================================================
namespace {
// 进程名归一化：小写 + 去首尾空白 + 去 .exe 后缀
std::wstring NormProc(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)std::towlower((wint_t)c);
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 4, 4, L".exe") == 0) s = s.substr(0, s.size() - 4);
    return s;
}
} // namespace

void RoomView::OpenWhitelist()
{
    // 以 FocusTracker 的现行名单为准（它才是判定依据），未启用检测时回落 settings
    m_wlApps = FocusTracker::Instance().UserApps();
    if (m_wlApps.empty() && !FocusTracker::Instance().Running())
        m_wlApps = CheckinStore::Instance().LoadSettings().focusApps;
    m_wlOpen = true;
    m_wlScroll = 0;
    m_wlHover = -1;
    m_wlHint.clear();
    m_wlHintT = 0.0f;
}

void RoomView::DebugForcePreview()
{
    // 截图自检：无头模式没有 FocusTracker，用默认名单铺满列表以便目视核对布局
    OpenWhitelist();
    if (m_wlApps.empty()) m_wlApps = FocusTracker::DefaultUserApps();
    m_wlA.Snap(1.0f);
}

void RoomView::CloseWhitelist()
{
    EndWlEdit();
    m_wlOpen = false;
    m_wlHover = -1;
}

void RoomView::WlCommit()
{
    // 归一化 + 落盘 + 即时生效（FocusTracker 下一秒轮询就按新名单判定）
    FocusTracker::Instance().SaveUserApps(m_wlApps);
    m_wlApps = FocusTracker::Instance().UserApps();
    int cap = (int)m_wlItemRects.size();
    if (cap <= 0) cap = 1;
    if (m_wlScroll > (int)m_wlApps.size() - cap) m_wlScroll = (int)m_wlApps.size() - cap;
    if (m_wlScroll < 0) m_wlScroll = 0;
}

void RoomView::WlAddText()
{
    std::wstring raw = m_wlEditHwnd ? ReadEditBuffer(m_wlEditHwnd) : m_wlText;
    // 允许一次填多个：空格 / 英文逗号 / 中文逗号 / 顿号 / 分号 分隔
    std::vector<std::wstring> toks;
    std::wstring cur;
    for (wchar_t c : raw) {
        if (c == L' ' || c == L',' || c == L'\uFF0C' || c == L'\u3001' || c == L';' || c == L'\uFF1B') {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) toks.push_back(cur);

    int added = 0, dup = 0;
    for (auto& t : toks) {
        std::wstring p = NormProc(t);
        if (p.empty()) continue;
        if (std::find(m_wlApps.begin(), m_wlApps.end(), p) != m_wlApps.end()) { dup++; continue; }
        m_wlApps.push_back(p);
        added++;
    }

    if (m_wlEditHwnd) { SetWindowTextW(m_wlEditHwnd, L""); SendMessageW(m_wlEditHwnd, EM_SETSEL, 0, 0); }
    m_wlText.clear();

    if (added > 0) {
        WlCommit();
        wchar_t buf[96];
        swprintf_s(buf, L"已加入 %d 项%s", added, dup > 0 ? L"（重复项已跳过）" : L"");
        m_wlHint = buf;
    } else if (dup > 0) {
        m_wlHint = L"名单里已经有了";
    } else {
        m_wlHint = L"请输入进程名，如 potplayer 或 chrome.exe";
    }
    m_wlHintT = 2.6f;
}

void RoomView::WlAddCurrentForeground()
{
    std::wstring p = FocusTracker::Instance().LastOtherApp();
    if (p.empty()) {
        m_wlHint = L"还没检测到其它前台应用 · 先切到目标应用停留一两秒再回来";
        m_wlHintT = 3.0f;
        return;
    }
    if (std::find(m_wlApps.begin(), m_wlApps.end(), p) != m_wlApps.end()) {
        m_wlHint = L"「" + p + L"」已在名单中";
        m_wlHintT = 2.4f;
        return;
    }
    m_wlApps.push_back(p);
    WlCommit();
    m_wlHint = L"已加入「" + p + L"」";
    m_wlHintT = 2.4f;
}

// ---- 白名单输入代理（隐藏 EDIT；面板固定于屏幕坐标，不随页面滚动）----
LRESULT CALLBACK RoomView::WlEditProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    RoomView* self = (RoomView*)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (self) {
        if (msg == WM_KEYDOWN && self->m_wlComp.empty()) {
            if (wp == VK_RETURN) { self->WlAddText(); return 0; }          // 回车即添加
            if (wp == VK_ESCAPE) { SetWindowTextW(w, L""); self->EndWlEdit(); return 0; }
        } else if (msg == WM_SETFOCUS) {
            int len = GetWindowTextLengthW(w);
            SendMessageW(w, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        } else if (msg == WM_KILLFOCUS) {
            self->m_wlEdit = false;   // 保留文本，点回来继续编辑
        }
    }
    WNDPROC old = self ? self->m_wlEditOld : nullptr;
    LRESULT r = old ? CallWindowProcW(old, w, msg, wp, lp) : DefWindowProcW(w, msg, wp, lp);

    // 中文 IME 组合串自绘（代理已 WM_SETREDRAW(FALSE)，不画就一片空白）；
    // 代理窗铺满输入框，候选窗默认贴其左上角即正确位置，无需额外定位。
    if (self) {
        if (msg == WM_IME_STARTCOMPOSITION) { self->m_wlComp.clear(); self->m_wlCompCaret = 0; }
        else if (msg == WM_IME_COMPOSITION) {
            lj::ImeReadComposition(w, self->m_wlComp, self->m_wlCompCaret);
            if (lp & GCS_RESULTSTR) self->m_wlComp.clear();
        } else if (msg == WM_IME_ENDCOMPOSITION) { self->m_wlComp.clear(); self->m_wlCompCaret = 0; }
    }

    if (msg == WM_SETFOCUS || msg == WM_KEYDOWN || msg == WM_CHAR ||
        msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_CHAR)
        HideCaret(w);
    return r;
}

void RoomView::EnsureWlEditor()
{
    if (m_wlEditHwnd) return;
    HWND parent = AppHwnd();
    if (!parent) return;
    m_wlEditHwnd = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                                   0, 0, 10, 10, parent, nullptr,
                                   (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
    if (!m_wlEditHwnd) return;
    m_wlEditOld = (WNDPROC)SetWindowLongPtrW(m_wlEditHwnd, GWLP_WNDPROC, (LONG_PTR)WlEditProc);
    SetWindowLongPtrW(m_wlEditHwnd, GWLP_USERDATA, (LONG_PTR)this);
    m_wlEditFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");
    if (m_wlEditFont) SendMessageW(m_wlEditHwnd, WM_SETFONT, (WPARAM)m_wlEditFont, TRUE);
    ShowWindow(m_wlEditHwnd, SW_HIDE);
}

void RoomView::BeginWlEdit()
{
    EnsureWlEditor();
    if (!m_wlEditHwnd) return;
    lj::SetEditBackdrop(AppPalette().paperHi);
    SetWindowTextW(m_wlEditHwnd, m_wlText.c_str());

    float s = (float)AppDpi() / 96.0f;
    int L = (int)(m_wlInputRect.left * s);
    int T = (int)(m_wlInputRect.top * s);
    int W = (int)((m_wlInputRect.right - m_wlInputRect.left) * s);
    int H = (int)((m_wlInputRect.bottom - m_wlInputRect.top) * s);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    SetWindowPos(m_wlEditHwnd, nullptr, L, T, W, H, SWP_NOZORDER);

    SendMessageW(m_wlEditHwnd, WM_SETREDRAW, FALSE, 0);
    ShowWindow(m_wlEditHwnd, SW_SHOW);
    SetFocus(m_wlEditHwnd);
    int len = GetWindowTextLengthW(m_wlEditHwnd);
    SendMessageW(m_wlEditHwnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    m_wlComp.clear(); m_wlCompCaret = 0;
}

void RoomView::EndWlEdit()
{
    if (m_wlEditHwnd) {
        if (!m_wlComp.empty()) lj::ImeCancelComposition(m_wlEditHwnd);
        m_wlComp.clear(); m_wlCompCaret = 0;
        m_wlText = ReadEditBuffer(m_wlEditHwnd);
        SendMessageW(m_wlEditHwnd, WM_SETREDRAW, TRUE, 0);
        ShowWindow(m_wlEditHwnd, SW_HIDE);
    }
    m_wlEdit = false;
}

void RoomView::LayoutWhitelist(const D2D1_RECT_F& area)
{
    float availH = area.bottom - area.top;
    float pw = 560.0f;
    float ph = (std::min)(540.0f, (std::max)(380.0f, availH - 48.0f));
    float cx = (area.left + area.right) * 0.5f;
    float cy = (area.top + area.bottom) * 0.5f;
    m_wlPanel = { cx - pw * 0.5f, cy - ph * 0.5f, cx + pw * 0.5f, cy + ph * 0.5f };

    float ix = m_wlPanel.left + 28.0f;
    float right = m_wlPanel.right - 28.0f;

    m_wlInputRect = { ix, m_wlPanel.top + 94.0f, right - 106.0f, m_wlPanel.top + 134.0f };
    m_wlAdd.bounds = { right - 96.0f, m_wlPanel.top + 94.0f, right, m_wlPanel.top + 134.0f };
    m_wlAddCur.bounds = { ix, m_wlPanel.top + 142.0f, right, m_wlPanel.top + 178.0f };

    float btnY = m_wlPanel.bottom - 28.0f - 42.0f;
    m_wlReset.bounds = { ix, btnY, ix + 132.0f, btnY + 42.0f };
    m_wlClose.bounds = { right - 132.0f, btnY, right, btnY + 42.0f };

    float listTop = m_wlPanel.top + 206.0f;
    float listBottom = btnY - 30.0f;
    if (listBottom < listTop + 30.0f) listBottom = listTop + 30.0f;
    m_wlListRect = { ix, listTop, right, listBottom };

    // 双列列表（列优先：先竖着排满一列再排下一列）
    const float cellH = 30.0f, colGap = 14.0f;
    int rows = (int)((listBottom - listTop) / cellH);
    if (rows < 1) rows = 1;
    int cap = rows * 2;
    float cellW = (right - ix - colGap) * 0.5f;

    if (m_wlScroll > (int)m_wlApps.size() - cap) m_wlScroll = (int)m_wlApps.size() - cap;
    if (m_wlScroll < 0) m_wlScroll = 0;

    m_wlItemRects.clear();
    m_wlItemIdx.clear();
    for (int k = 0; k < cap; ++k) {
        int idx = m_wlScroll + k;
        if (idx >= (int)m_wlApps.size()) break;
        int col = k / rows, row = k % rows;
        float l = ix + col * (cellW + colGap);
        float t = listTop + row * cellH;
        m_wlItemRects.push_back({ l, t, l + cellW, t + cellH - 5.0f });
        m_wlItemIdx.push_back(idx);
    }
}

void RoomView::UpdateWhitelist(float dt, const Input& in)
{
    UpdateWidgets(m_wlWidgets, dt, in);   // 面板固定于屏幕坐标，不加 ScrollY

    if (m_wlHintT > 0.0f) m_wlHintT -= dt;

    // 一键加入按钮文案随最近前台应用变化
    {
        std::wstring last = FocusTracker::Instance().LastOtherApp();
        m_wlAddCur.label = last.empty() ? std::wstring(L"＋ 加入刚才用的应用")
                                        : (L"＋ 加入刚才用的应用：" + last);
    }

    // 悬停项
    m_wlHover = -1;
    for (size_t k = 0; k < m_wlItemRects.size(); ++k) {
        const auto& r = m_wlItemRects[k];
        if (in.mouseX >= r.left && in.mouseX <= r.right && in.mouseY >= r.top && in.mouseY <= r.bottom) {
            m_wlHover = (int)k; break;
        }
    }

    // 列表滚轮翻看（in.wheel 正 = 向下滚）
    if (in.wheel != 0.0f &&
        in.mouseX >= m_wlListRect.left && in.mouseX <= m_wlListRect.right &&
        in.mouseY >= m_wlListRect.top && in.mouseY <= m_wlListRect.bottom) {
        m_wlScroll += (in.wheel > 0.0f) ? 2 : -2;
        if (m_wlScroll < 0) m_wlScroll = 0;
    }

    if (in.clicked) {
        // 逐项删除（点击项右侧 × 热区）
        for (size_t k = 0; k < m_wlItemRects.size(); ++k) {
            const auto& r = m_wlItemRects[k];
            if (in.mouseX >= r.right - 30.0f && in.mouseX <= r.right &&
                in.mouseY >= r.top && in.mouseY <= r.bottom) {
                int idx = m_wlItemIdx[k];
                if (idx >= 0 && idx < (int)m_wlApps.size()) {
                    std::wstring gone = m_wlApps[(size_t)idx];
                    m_wlApps.erase(m_wlApps.begin() + idx);
                    WlCommit();
                    m_wlHint = L"已移出「" + gone + L"」";
                    m_wlHintT = 2.4f;
                }
                return;
            }
        }
        // 输入框 → 进编辑
        if (in.mouseX >= m_wlInputRect.left && in.mouseX <= m_wlInputRect.right &&
            in.mouseY >= m_wlInputRect.top && in.mouseY <= m_wlInputRect.bottom) {
            if (!m_wlEdit) { m_wlEdit = true; BeginWlEdit(); }
            return;
        }
        // 点面板外 → 关闭
        bool inPanel = (in.mouseX >= m_wlPanel.left && in.mouseX <= m_wlPanel.right &&
                        in.mouseY >= m_wlPanel.top && in.mouseY <= m_wlPanel.bottom);
        if (!inPanel) { CloseWhitelist(); return; }
    }

    if (m_wlEdit && m_wlEditHwnd) m_wlText = ReadEditBuffer(m_wlEditHwnd);
}

void RoomView::PaintWhitelist(Canvas& cv)
{
    float a = m_wlA.value;
    if (a <= 0.004f) return;
    const auto& pal = cv.Pal();

    // 背板：压暗下层，明确「模态」
    cv.FillRect(m_area, WithAlpha(pal.ink900, 0.34f * a));

    cv.PushOpacity(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ease::OutCubic(a)) * 12.0f));

    cv.FillRoundRect(m_wlPanel, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_wlPanel, shape::kEdge, pal.ruleStrong, shape::kStroke);
    cv.CornerTicks({ m_wlPanel.left + 8.0f, m_wlPanel.top + 8.0f,
                     m_wlPanel.right - 8.0f, m_wlPanel.bottom - 8.0f },
                   WithAlpha(pal.ink300, 0.45f), 11.0f, 1.0f);

    float ix = m_wlPanel.left + 28.0f;
    float right = m_wlPanel.right - 28.0f;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.2f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 专注白名单", { ix, m_wlPanel.top + 22.0f, right, m_wlPanel.top + 38.0f }, sec, pal.ink300);
    cv.PerforationH(ix + 150.0f, right, m_wlPanel.top + 30.0f, WithAlpha(pal.ruleStrong, 0.5f));

    TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.5f; ds.vAlign = VAlign::Top;
    cv.Text(L"专注时前台若是名单内的应用（如播放老师视频的播放器），计时照常走；"
            L"切到名单以外的程序则自动暂停。名单留空 = 回退内置智能判定。",
            { ix, m_wlPanel.top + 42.0f, right, m_wlPanel.top + 88.0f }, ds, pal.ink500);

    // 输入框
    bool editing = m_wlEdit;
    cv.FillRoundRect(m_wlInputRect, shape::kEdgeSoft, editing ? pal.paperHi : WithAlpha(pal.rule, 0.42f));
    cv.StrokeRoundRect(m_wlInputRect, shape::kEdgeSoft, editing ? pal.seal : pal.rule,
                       editing ? shape::kStroke : shape::kHair);
    TextStyle is; is.role = FontRole::Sans; is.size = 13.0f; is.vAlign = VAlign::Middle;
    D2D1_RECT_F tbox{ m_wlInputRect.left + 12.0f, m_wlInputRect.top,
                      m_wlInputRect.right - 12.0f, m_wlInputRect.bottom };
    if (editing) {
        PaintFieldEditIme(cv, tbox, is, m_wlText, EditCaretPos(m_wlEditHwnd),
                          m_wlComp, m_wlCompCaret, pal.ink900, 0.0f);
    } else if (!m_wlText.empty()) {
        cv.Text(m_wlText, tbox, is, pal.ink900);
    } else {
        cv.Text(L"输入进程名后回车，如 potplayer / chrome.exe（可空格分隔多个）",
                tbox, is, WithAlpha(pal.ink300, 0.95f));
    }
    m_wlAdd.Paint(cv);
    m_wlAddCur.Paint(cv);

    // 操作反馈
    if (m_wlHintT > 0.0f && !m_wlHint.empty()) {
        TextStyle hs; hs.role = FontRole::Sans; hs.size = 11.5f; hs.vAlign = VAlign::Middle;
        cv.PushOpacity(Clamp01(m_wlHintT / 0.6f));
        cv.Text(m_wlHint, { ix, m_wlPanel.top + 182.0f, right, m_wlPanel.top + 202.0f }, hs, pal.seal);
        cv.PopOpacity();
    }

    // 名单列表
    if (m_wlApps.empty()) {
        TextStyle es; es.role = FontRole::Sans; es.size = 12.5f; es.vAlign = VAlign::Top;
        cv.Text(L"名单为空 —— 当前回退内置智能判定：浏览器 / 播放器 / 笔记类进程，"
                L"且窗口标题需含学习关键词才算专注。",
                { m_wlListRect.left, m_wlListRect.top + 6.0f, m_wlListRect.right, m_wlListRect.top + 60.0f },
                es, pal.ink500);
    } else {
        TextStyle ns; ns.role = FontRole::Mono; ns.size = 12.5f; ns.vAlign = VAlign::Middle;
        TextStyle xs; xs.role = FontRole::Sans; xs.size = 14.0f;
        xs.vAlign = VAlign::Middle; xs.hAlign = HAlign::Center;
        for (size_t k = 0; k < m_wlItemRects.size(); ++k) {
            const auto& r = m_wlItemRects[k];
            bool hov = ((int)k == m_wlHover);
            cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(pal.rule, hov ? 0.62f : 0.34f));
            cv.Text(m_wlApps[(size_t)m_wlItemIdx[k]],
                    { r.left + 12.0f, r.top, r.right - 32.0f, r.bottom }, ns, pal.ink700);
            cv.Text(L"×", { r.right - 30.0f, r.top, r.right, r.bottom }, xs,
                    hov ? pal.seal : WithAlpha(pal.ink300, 0.9f));
        }
        // 计数 / 翻看提示
        TextStyle cs; cs.role = FontRole::Mono; cs.size = 10.5f; cs.letterSpacing = 1.2f;
        int from = m_wlScroll + 1;
        int to = m_wlScroll + (int)m_wlItemRects.size();
        wchar_t cbuf[96];
        swprintf_s(cbuf, L"第 %d–%d / 共 %d 项 · 滚轮翻看 · 点 × 移出",
                   from, to, (int)m_wlApps.size());
        cv.Text(cbuf, { m_wlListRect.left, m_wlListRect.bottom + 4.0f,
                        m_wlListRect.right, m_wlListRect.bottom + 22.0f }, cs, pal.ink300);
    }

    m_wlReset.Paint(cv);
    m_wlClose.Paint(cv);

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  #71 专注结束提醒（应用内提示条 + 托盘气泡 + 任务栏闪烁）
// ============================================================
void RoomView::ShowToast(const std::wstring& msg)
{
    m_toastMsg = msg;
    m_toastT = 6.0f;
}

void RoomView::PaintToast(Canvas& cv)
{
    if (m_toastT <= 0.0f || m_toastMsg.empty()) return;
    const auto& pal = cv.Pal();
    float a = Clamp01(m_toastT / 0.6f);            // 末段淡出
    a *= Clamp01((6.0f - m_toastT) / 0.18f);       // 起始淡入

    float w = 440.0f, h = 58.0f;
    float cx = (m_area.left + m_area.right) * 0.5f;
    float y = m_area.bottom - 108.0f;
    D2D1_RECT_F r{ cx - w * 0.5f, y, cx + w * 0.5f, y + h };

    cv.PushOpacity(a);
    cv.FillRoundRect(r, shape::kEdge, pal.ink900);
    cv.StrokeRoundRect(r, shape::kEdge, WithAlpha(pal.seal, 0.9f), shape::kStroke);
    cv.FillCircle(r.left + 26.0f, (r.top + r.bottom) * 0.5f, 6.0f, pal.seal);
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f;
    ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.vAlign = VAlign::Middle;
    cv.Text(m_toastMsg, { r.left + 44.0f, r.top, r.right - 20.0f, r.bottom }, ts, pal.paper);
    cv.PopOpacity();
}

void RoomView::EnterRoom(const std::wstring& id)
{
    m_room = id;
    m_inRoom = true;
    m_timer = TimerState::Idle;
    m_remain = (float)m_presetMin * 60.0f;
    m_t = 0.0f;
    m_roomBuilt = false;   // 重建房间控件并重新入场
    Realtime::Instance().JoinRoom(net::ToUtf8(m_room));
    SendMyPresence();
}

void RoomView::LeaveRoom()
{
    CancelFocus();         // 离开即取消本次专注（与网页一致：专注为用户本地独立状态）
    m_inRoom = false;
    m_t = 0.0f;
    m_selBuilt = false;    // 重建选择控件
}

void RoomView::BuildSelectWidgets()
{
    for (int i = 0; i < 3; ++i) {
        m_roomCards[i].icon = kRooms[i].icon;
        m_roomCards[i].name = kRooms[i].name;
        m_roomCards[i].desc = kRooms[i].desc;
        m_roomCards[i].accent = kRooms[i].accent;
        m_roomCards[i].accentSet = true;
        int idx = i;
        m_roomCards[i].onClick = [this, idx] { EnterRoom(kRooms[idx].id); };
        m_roomCards[i].StartEnter(StaggerDelay(i, 0.06f) + 0.30f);
    }
    m_selBack.label = L"返 回 首 页"; m_selBack.tag = L"00"; m_selBack.fontSize = 13.0f;
    m_selBack.onClick = [this] { Go(L"home"); };
    m_selBack.StartEnter(0.50f);
    m_selBuilt = true;
}

void RoomView::BuildRoomWidgets()
{
    const int presets[5] = { 15, 25, 45, 60, 90 };
    for (int i = 0; i < 5; ++i) {
        m_presetBtns[i].label = std::to_wstring(presets[i]);
        m_presetBtns[i].fontSize = 13.0f;
        m_presetBtns[i].primary = false;
        int p = presets[i];
        m_presetBtns[i].onClick = [this, p] {
            m_presetMin = p;
            if (m_timer == TimerState::Idle) m_remain = (float)p * 60.0f;
        };
        m_presetBtns[i].StartEnter(StaggerDelay(i, 0.05f) + 0.30f);
    }
    m_stepperMinus.label = L"−"; m_stepperMinus.fontSize = 18.0f;
    m_stepperMinus.onClick = [this] {
        if (m_presetMin > 1) { m_presetMin--; if (m_timer == TimerState::Idle) m_remain = (float)m_presetMin * 60.0f; }
    };
    m_stepperPlus.label = L"+"; m_stepperPlus.fontSize = 18.0f;
    m_stepperPlus.onClick = [this] {
        if (m_presetMin < 240) { m_presetMin++; if (m_timer == TimerState::Idle) m_remain = (float)m_presetMin * 60.0f; }
    };
    m_startBtn.label = L"开 始 专 注"; m_startBtn.primary = true; m_startBtn.fontSize = 14.0f;
    m_startBtn.onClick = [this] { ToggleStart(); };
    m_resetBtn.label = L"重 置"; m_resetBtn.fontSize = 13.0f;
    m_resetBtn.onClick = [this] { ResetTimer(); };

    // P1-5 #71 联动开关：点击切换「番茄钟是否随前台学习状态自动暂停」
    m_linkBtn.label = L"联动前台：开"; m_linkBtn.fontSize = 11.0f; m_linkBtn.primary = false;
    m_linkBtn.onClick = [this] { m_linkFocus = !m_linkFocus; m_focusLost = false; };

    // #71 白名单入口：打开「专注白名单」表单
    m_wlBtn.label = L"白名单"; m_wlBtn.fontSize = 11.0f; m_wlBtn.primary = false;
    m_wlBtn.onClick = [this] { OpenWhitelist(); };

    // #71 弹层内按钮
    m_wlAdd.label = L"添 加"; m_wlAdd.fontSize = 12.5f; m_wlAdd.primary = true;
    m_wlAdd.onClick = [this] { WlAddText(); };
    m_wlAddCur.label = L"＋ 加入当前前台进程"; m_wlAddCur.fontSize = 12.5f; m_wlAddCur.primary = false;
    m_wlAddCur.onClick = [this] { WlAddCurrentForeground(); };
    m_wlReset.label = L"恢 复 默 认"; m_wlReset.fontSize = 12.5f; m_wlReset.primary = false;
    m_wlReset.onClick = [this] {
        m_wlApps = FocusTracker::DefaultUserApps();
        m_wlScroll = 0;
        WlCommit();
        m_wlHint = L"已恢复内置默认名单"; m_wlHintT = 2.4f;
    };
    m_wlClose.label = L"完 成"; m_wlClose.fontSize = 12.5f; m_wlClose.primary = true;
    m_wlClose.onClick = [this] { CloseWhitelist(); };
    m_wlWidgets.clear();
    m_wlWidgets.push_back(&m_wlAdd);
    m_wlWidgets.push_back(&m_wlAddCur);
    m_wlWidgets.push_back(&m_wlReset);
    m_wlWidgets.push_back(&m_wlClose);
    for (auto* w : m_wlWidgets) w->StartEnter(0.0f);

    m_leaveBtn.label = L"更 换 自 习 室"; m_leaveBtn.tag = L"↩"; m_leaveBtn.fontSize = 12.5f;
    m_leaveBtn.onClick = [this] { LeaveRoom(); };
    m_backBtn.label = L"返 回 首 页"; m_backBtn.tag = L"00"; m_backBtn.fontSize = 13.0f;
    m_backBtn.onClick = [this] { Go(L"home"); };

    m_overlayPause.label = L"暂 停"; m_overlayPause.primary = true; m_overlayPause.fontSize = 14.0f;
    m_overlayPause.onClick = [this] {
        if (m_timer == TimerState::Running) m_timer = TimerState::Paused;
        else if (m_timer == TimerState::Paused) m_timer = TimerState::Running;
    };
    m_overlayCancel.label = L"取 消"; m_overlayCancel.fontSize = 14.0f;
    m_overlayCancel.onClick = [this] { CancelFocus(); };

    m_overlayWidgets.clear();
    m_overlayWidgets.push_back(&m_overlayPause);
    m_overlayWidgets.push_back(&m_overlayCancel);
    for (auto* w : m_overlayWidgets) w->StartEnter(0.0f);

    // §3 聊天发送按钮
    // 直接读隐藏 EDIT 代理的文本发送（不依赖 m_compose 同步时序），
    // 发送后清空代理、保持编辑态可继续输入；点输入框外部才退出编辑。
    m_sendBtn.label = L"发送"; m_sendBtn.fontSize = 13.0f;
    m_sendBtn.onClick = [this] {
        if (!m_chatEditHwnd) return;
        std::wstring text = ReadEditBuffer(m_chatEditHwnd);
        if (!text.empty()) Realtime::Instance().SendChat(net::ToUtf8(text));
        m_compose.clear();
        // 清空代理文本与光标：连续发送不退出编辑态
        SetWindowTextW(m_chatEditHwnd, L"");
        SendMessageW(m_chatEditHwnd, EM_SETSEL, 0, 0);
    };
    m_sendBtn.StartEnter(0.0f);

    m_roomBuilt = true;
}

void RoomView::ToggleStart()
{
    if (m_timer == TimerState::Idle) {
        if (m_remain <= 0.0f) m_remain = (float)m_presetMin * 60.0f;
        m_focusStart = (long long)std::time(nullptr);
        m_timer = TimerState::Running;
    } else if (m_timer == TimerState::Running) {
        m_timer = TimerState::Paused;
    } else {
        m_timer = TimerState::Running;
    }
    SendMyPresence();
}

bool RoomView::HotkeyToggleTimer()
{
    // 复用现有切换逻辑（Idle→Running / Running→Paused / Paused→Running）
    ToggleStart();
    return m_timer == TimerState::Running;
}

void RoomView::ResetTimer()
{
    m_timer = TimerState::Idle;
    m_remain = (float)m_presetMin * 60.0f;
}

void RoomView::CancelFocus()
{
    m_timer = TimerState::Idle;
    m_remain = (float)m_presetMin * 60.0f;
}

void RoomView::CompleteFocus()
{
    FocusSession fs;
    fs.date = FormatDate(Today());
    fs.start = m_focusStart;
    fs.end = (long long)std::time(nullptr);
    fs.min = m_presetMin;
    fs.tag = L"自习室";
    CheckinStore::Instance().AddFocus(fs);
    m_timer = TimerState::Idle;
    m_remain = (float)m_presetMin * 60.0f;
    RecomputeStats();
    SendMyPresence();

    // ---- #71 专注结束提醒 ----
    // 用户可能正盯着别的窗口（看网课视频），所以三路都要给：
    //   ① 应用内提示条（回到芙洛理一眼看见）
    //   ② 系统托盘气泡（窗口最小化/在后台也能弹出来）
    //   ③ 任务栏图标闪烁（气泡被系统「专注助手」屏蔽时的兜底）
    wchar_t msg[128];
    swprintf_s(msg, L"本轮 %d 分钟专注已完成 · 今日累计 %d 分钟，起来活动一下",
               m_presetMin, m_todayMin);
    ShowToast(msg);
    TrayIcon::Instance().Balloon(L"芙洛理 · 专注结束", msg, NIIF_INFO);
    if (HWND hw = AppHwnd()) {
        FLASHWINFO fi{};
        fi.cbSize = sizeof(fi);
        fi.hwnd = hw;
        fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        fi.uCount = 3;
        fi.dwTimeout = 0;
        FlashWindowEx(&fi);
    }
}

void RoomView::RecomputeStats()
{
    auto all = CheckinStore::Instance().LoadFocus();
    m_totalCount = (int)all.size();
    std::wstring td = FormatDate(Today());
    m_todayMin = 0;
    for (auto& s : all) if (s.date == td) m_todayMin += s.min;
    m_history = all;
    if ((int)m_history.size() > 30) m_history.erase(m_history.begin(), m_history.end() - 30);
    std::reverse(m_history.begin(), m_history.end());   // 最新在上
}

// ============================================================
//  布局
// ============================================================
void RoomView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    if (m_inRoom) {
        if (!m_roomBuilt) BuildRoomWidgets();
        LayoutRoom(area, cv);
    } else {
        if (!m_selBuilt) BuildSelectWidgets();
        LayoutSelect(area);
    }
    LayoutWhitelist(area);   // #71 弹层固定于屏幕坐标，与页面滚动无关
}

void RoomView::LayoutSelect(const D2D1_RECT_F& area)
{
    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    float cardH = 92.0f, gap = 16.0f;
    for (int i = 0; i < 3; ++i) {
        float top = flow.block(cardH + gap).top;
        m_roomCards[i].bounds = { x0, top, x0 + contentW, top + cardH };
    }
    flow.block(16.0f);
    {
        float top = flow.block(46.0f + 30.0f).top;
        m_selBack.bounds = { x0, top, x0 + 150.0f, top + 46.0f };
    }
    SetContentHeight(flow.cursorY - area.top);

    m_widgets.clear();
    for (int i = 0; i < 3; ++i) m_widgets.push_back(&m_roomCards[i]);
    m_widgets.push_back(&m_selBack);
}

void RoomView::LayoutRoom(const D2D1_RECT_F& area, Canvas& cv)
{
    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    float colGap = 24.0f;
    float leftW = contentW * 0.62f;
    float rightW = contentW - leftW - colGap;
    float colTop = flow.cursorY;        // 左右两栏共享的顶边

    // 计时卡（左）—— 卡内 7 段竖向控件链同样走流式，
    // 否则任一段改高度都要手工顺延后面全部锚点，漏一处即重叠
    m_timerCard = { x0, colTop, x0 + leftW, 0.0f };
    const float kStatusH = 26.0f;   // P1-5 #71 联动状态条：插在进度条与预设之间
    {
        lj::ui::VLayout tf(m_timerCard.left + 26.0f, m_timerCard.top + 26.0f + 8.0f,
                           leftW - 52.0f, 0.0f);
        m_countdownY = tf.block(96.0f + 16.0f + kStatusH).top;
        m_presetY    = tf.block(44.0f + 12.0f).top;
        m_stepperY   = tf.block(44.0f + 16.0f).top;
        m_ctrlY      = tf.block(64.0f + 16.0f).top;
        m_arrY       = tf.block(56.0f + 8.0f).top;
        m_statY      = tf.block(40.0f + 18.0f).top;
        m_musicY     = tf.block(96.0f + 22.0f).top;
        m_timerCard.bottom = tf.cursorY;
    }

    // 计时卡右上一排三枚（自左向右）：白名单 · 联动开关 · 更换自习室
    // 统一下移到 top+40 避开 SECTION 标题与分割线，彼此留 12px 间隙不重叠。
    m_wlBtn.bounds = { m_timerCard.right - 372.0f, m_timerCard.top + 40.0f,
                       m_timerCard.right - 290.0f, m_timerCard.top + 72.0f };
    m_linkBtn.bounds = { m_timerCard.right - 278.0f, m_timerCard.top + 40.0f,
                         m_timerCard.right - 162.0f, m_timerCard.top + 72.0f };
    // #28 播放控制：note 区右侧 = 播放/暂停按钮 + 音量滑条（自绘）
    {
        float bx = m_timerCard.right - 26.0f - 110.0f - 10.0f - 96.0f;   // 音量条左端
        m_volRect = { bx, m_musicY + 22.0f + 28.0f, bx + 110.0f, m_musicY + 22.0f + 46.0f };
        float pbX = m_volRect.right + 10.0f;
        m_musicBtn.bounds = { pbX, m_musicY + 22.0f + 14.0f, pbX + 96.0f, m_musicY + 22.0f + 14.0f + 36.0f };
        m_musicBtn.label = L"▶ 播放";
    }

    // 成员卡（右）—— 高度随在场人数动态
    int memRows = (std::min)((int)m_members.size(), 12);
    float memH = 52.0f + (memRows > 0 ? (float)memRows * 28.0f : 30.0f) + 16.0f;
    m_membersCard = { x0 + leftW + colGap, colTop, x0 + contentW, colTop + memH };

    // 计时控件定位
    float innerX = m_timerCard.left + 26.0f;
    float innerW = m_timerCard.right - innerX - 26.0f;
    float pbW = (innerW - 4.0f * 10.0f) / 5.0f;
    for (int i = 0; i < 5; ++i)
        m_presetBtns[i].bounds = { innerX + i * (pbW + 10.0f), m_presetY,
                                   innerX + i * (pbW + 10.0f) + pbW, m_presetY + 40.0f };

    float stepW = 44.0f, valW = 120.0f;
    float stepTotal = stepW + 10.0f + valW + 10.0f + stepW;
    float stepX = innerX + (innerW - stepTotal) * 0.5f;
    m_stepperMinus.bounds = { stepX, m_stepperY, stepX + stepW, m_stepperY + 40.0f };
    m_stepperPlus.bounds = { stepX + stepW + 10.0f + valW + 10.0f, m_stepperY,
                             stepX + stepW + 10.0f + valW + 10.0f + stepW, m_stepperY + 40.0f };

    float startW = innerW * 0.62f;
    m_startBtn.bounds = { innerX, m_ctrlY, innerX + startW, m_ctrlY + 50.0f };
    m_resetBtn.bounds = { innerX + startW + 12.0f, m_ctrlY, innerX + innerW, m_ctrlY + 50.0f };

    // 更换自习室（计时卡右上 · 与联动开关并排，下移避开 SECTION 标题与分割线）
    m_leaveBtn.bounds = { m_timerCard.right - 150.0f, m_timerCard.top + 40.0f,
                          m_timerCard.right - 18.0f, m_timerCard.top + 72.0f };

    // 双栏区占位：高度取左右两栏中较低者，再由 flow 统一推进
    float hy = flow.block((std::max)(m_timerCard.bottom, m_membersCard.bottom) - colTop + 24.0f).bottom;

    // 历史卡（整宽）
    int histRows = (std::min)((int)m_history.size(), 30);
    if (histRows == 0) histRows = 1;
    float histH = 42.0f + (float)histRows * 30.0f + 16.0f;
    m_histCard = { x0, hy, x0 + contentW, hy + histH };
    m_histY = hy + 42.0f;
    flow.block(histH + 24.0f);

    // 公共聊天（整宽）—— 高度随消息数动态
    bool muted = (m_room == L"silent");
    int chatRows = (std::min)((int)m_chat.size(), 8);
    float chatContentH = chatRows > 0 ? (float)chatRows * 24.0f + 12.0f : 28.0f;
    float chatInputH = muted ? 0.0f : (40.0f + 12.0f);
    float chatH = 34.0f + chatContentH + chatInputH + 16.0f;
    float cy = flow.cursorY;
    m_chatCard = { x0, cy, x0 + contentW, cy + chatH };
    m_chatY = cy + 34.0f;
    flow.block(chatH + 24.0f);

    // 聊天输入框 + 发送按钮定位（静音房不显示）
    float cix = m_chatCard.left + 26.0f;
    float cright = m_chatCard.right - 26.0f;
    if (!muted) {
        m_chatInputRect = { cix, m_chatY + chatContentH + 6.0f, cright - 90.0f, m_chatY + chatContentH + 6.0f + 40.0f };
        m_chatSendRect  = { cright - 80.0f, m_chatY + chatContentH + 6.0f, cright, m_chatY + chatContentH + 6.0f + 40.0f };
        m_sendBtn.bounds = m_chatSendRect;
    } else {
        m_chatInputRect = {}; m_chatSendRect = {};
    }

    // 返回首页
    {
        float by = flow.block(46.0f + 30.0f).top;
        m_backBtn.bounds = { x0, by, x0 + 150.0f, by + 46.0f };
    }
    SetContentHeight(flow.cursorY - area.top);

    m_widgets.clear();
    m_widgets.push_back(&m_leaveBtn);
    for (int i = 0; i < 5; ++i) m_widgets.push_back(&m_presetBtns[i]);
    m_widgets.push_back(&m_stepperMinus);
    m_widgets.push_back(&m_stepperPlus);
    m_widgets.push_back(&m_startBtn);
    m_widgets.push_back(&m_resetBtn);
    m_widgets.push_back(&m_linkBtn);
    m_widgets.push_back(&m_wlBtn);
    m_widgets.push_back(&m_backBtn);
    if (m_room != L"silent") m_widgets.push_back(&m_sendBtn);

    // 覆盖层按钮（屏幕坐标，固定于内容区底部居中）
    float obW = 150.0f;
    float obY = area.bottom - 90.0f;
    float cx = area.left + availW * 0.5f;
    m_overlayPause.bounds = { cx - obW - 12.0f, obY, cx - 12.0f, obY + 50.0f };
    m_overlayCancel.bounds = { cx + 12.0f, obY, cx + 12.0f + obW, obY + 50.0f };
}

// ============================================================
//  更新
// ============================================================
void RoomView::Update(float dt, const Input& in)
{
    // #71 白名单弹层为模态：屏蔽页面滚动，避免弹层浮在半空而底页乱跑
    Input base = in;
    if (m_wlOpen) base.wheel = 0.0f;
    View::Update(dt, base);
    m_t += dt;
    if (m_toastT > 0.0f) m_toastT -= dt;
    m_wlA.target = m_wlOpen ? 1.0f : 0.0f;
    m_wlA.Update(dt);

    if (m_timer == TimerState::Running) {
        // P1-5 #71 番茄钟 × 前台学习联动：检测到离开学习（前台非学习窗口或空闲）
        // 时自动暂停倒计时，回到学习窗口后继续；联动关闭则无视前台状态。
        bool tick = true;
        if (m_linkFocus && FocusTracker::Instance().Running()) {
            FocusState st = FocusTracker::Instance().Snapshot();
            if (st.studying) {
                m_focusLost = false;
            } else {
                m_focusLost = true;
                m_focusApp = st.process.empty() ? L"桌面" : st.process;
                tick = false;   // 摸鱼/空闲 → 不递减，等效自动暂停
            }
        } else {
            m_focusLost = false;
        }
        if (tick) {
            m_remain -= dt;
            if (m_remain <= 0.0f) { m_remain = 0.0f; CompleteFocus(); }
        }
    } else {
        m_focusLost = false;
    }

    bool active = (m_timer == TimerState::Running || m_timer == TimerState::Paused);
    m_overlayA.target = active ? 1.0f : 0.0f;
    m_overlayA.Update(dt);

    if (m_timer == TimerState::Idle)        m_startBtn.label = L"开 始 专 注";
    else if (m_timer == TimerState::Running) m_startBtn.label = L"暂 停";
    else                                     m_startBtn.label = L"继 续 专 注";

    m_linkBtn.label = m_linkFocus ? L"联动前台：开" : L"联动前台：关";
    {
        int n = (int)m_wlApps.size();
        m_wlBtn.label = n > 0 ? (L"白名单 " + std::to_wstring(n)) : std::wstring(L"白名单");
    }

    if (m_timer == TimerState::Running) m_overlayPause.label = L"暂 停";
    else                                 m_overlayPause.label = L"继 续 专 注";

    // #71 弹层打开时独占输入：底层控件与聊天框一律不响应，防误触
    if (m_wlOpen) {
        UpdateWhitelist(dt, in);
        m_overInteractive = true;
        if (m_netDirty.exchange(false)) SnapshotNet();
        return;
    }

    if (active) {
        UpdateWidgets(m_overlayWidgets, dt, in);   // 屏幕坐标
        m_overInteractive = false;
        for (auto* w : m_overlayWidgets) if (w->Hovered()) m_overInteractive = true;
    } else {
        Input shifted = in;
        shifted.mouseY = in.mouseY + ScrollY();
        UpdateWidgets(m_widgets, dt, shifted);
        m_overInteractive = false;
        for (auto* w : m_widgets) if (w->Hovered()) m_overInteractive = true;
    }

    // ---- §3 云端实时快照消费 ----
    if (m_netDirty.exchange(false)) SnapshotNet();

    // ---- #28 背景音乐按钮（房间内、非专注遮挡时）----
    if (m_inRoom && !active) {
        auto ps = MusicPlayer::Instance().GetState();
        m_musicBtn.label = (ps == MusicPlayer::State::Playing) ? L"暂停" : L"播放";
        // 让按钮自身维护 hover / press 态（供矢量图标绘制）
        Input shifted = in;
        shifted.mouseY = in.mouseY + ScrollY();
        m_musicBtn.Update(dt, shifted);
        // 音量滑条：按下（含拖动）→ 换算音量
        // 注意：m_volRect 是内容坐标，须加 ScrollY() 折算回屏幕坐标（与播放按钮一致），
        // 否则页面下滚后滑条实际位置与命中区域错位 → 点不动。
        const auto& vr = m_volRect;
        float vsy = in.mouseY + ScrollY();
        bool onVol = (vr.right > vr.left && in.mouseX >= vr.left && in.mouseX <= vr.right &&
                      vsy >= vr.top && vsy <= vr.bottom);
        if (m_volDrag) {
            MusicVolumeFromX(in.mouseX);
            if (in.released) m_volDrag = false;
        } else if (in.pressed && onVol) {
            m_volDrag = true;
            MusicVolumeFromX(in.mouseX);
        }
        if (in.clicked) {
            float sy = in.mouseY + ScrollY();
            const auto& b = m_musicBtn.bounds;
            if (in.mouseX >= b.left && in.mouseX <= b.right && sy >= b.top && sy <= b.bottom) { PlayMusic(); return; }
        }
    }

    // ---- §3 聊天输入（房间内且非静音房）----
    // 输入由隐藏 EDIT 代理接管（含中文 IME）：点击进编辑，Enter 发送，Esc 清空退出，
    // 失焦保留文本；这里只负责进入编辑态 + 每帧同步显示缓冲。
    if (m_inRoom && m_room != L"silent" && !active) {
        if (in.clicked) {
            float sy = in.mouseY + ScrollY();
            bool hit = (in.mouseX >= m_chatInputRect.left && in.mouseX <= m_chatInputRect.right &&
                        sy >= m_chatInputRect.top && sy <= m_chatInputRect.bottom);
            if (hit && !m_chatEdit) { m_chatEdit = true; BeginChatEdit(); }
            // 点外部：EDIT 失焦（WM_KILLFOCUS → EndChatEdit(false)），这里不用管
        }
        if (m_chatEdit && m_chatEditHwnd) {
            m_compose = ReadEditBuffer(m_chatEditHwnd);   // 每帧同步已提交文本，供绘制
            SyncChatEditorRect();                         // 跟随滚动，保证点击命中与候选窗定位
        }
    }
}

// ============================================================
//  绘制
// ============================================================
void RoomView::Paint(Canvas& cv)
{
    float availW = m_area.right - m_area.left;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    if (m_inRoom) PaintRoom(cv);
    else           PaintSelect(cv);

    cv.PopTransform();
    cv.PopClip();

    // 静默专注覆盖层（固定，不随滚动）
    if (m_overlayA.value > 0.004f) PaintFocusOverlay(cv);

    // #71 白名单弹层与专注结束提示条（固定于屏幕坐标，压在最上层）
    PaintWhitelist(cv);
    PaintToast(cv);
}

void RoomView::PaintSelect(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_contentTop;

    float ha = Clamp01(m_t / 0.5f);
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.PushOpacity(ha);
    cv.Text(L"No.02 · 卷宗", { x0, y0, x0 + 320.0f, y0 + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 36.0f; h1.weight = DWRITE_FONT_WEIGHT_BLACK;
    h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"自 习 室", x0, y0 + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    TextStyle sub; sub.role = FontRole::Sans; sub.size = 12.5f; sub.hAlign = HAlign::Right;
    cv.PushOpacity(Clamp01((m_t - 0.2f) / 0.5f));
    cv.Text(L"选择一间自习室进入，实时看见在场同学、一起计时与聊天",
            { x0 + contentW - 560.0f, y0 + 4.0f, x0 + contentW, y0 + 24.0f }, sub, pal.ink500);
    cv.PopOpacity();

    cv.PerforationH(x0, x0 + contentW, y0 + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));

    for (auto& c : m_roomCards) c.Paint(cv);
    m_selBack.Paint(cv);
}

void RoomView::PaintRoom(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_contentTop;

    float ha = Clamp01(m_t / 0.5f);
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.PushOpacity(ha);
    cv.Text(L"No.02 · 卷宗", { x0, y0, x0 + 320.0f, y0 + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    std::wstring rname = L"自习室";
    for (int i = 0; i < 3; ++i) if (kRooms[i].id == m_room) rname = kRooms[i].name;
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 36.0f; h1.weight = DWRITE_FONT_WEIGHT_BLACK;
    h1.letterSpacing = 3.0f;
    cv.CharsReveal(rname, x0, y0 + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);
    cv.PushOpacity(ha);
    cv.FillCircle(x0 + cv.MeasureWidth(rname, h1) + 28.0f, y0 + 44.0f, 5.0f, pal.jade);
    cv.PopOpacity();

    cv.PerforationH(x0, x0 + contentW, y0 + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));

    PaintTimerCard(cv);
    m_leaveBtn.Paint(cv);
    PaintMembersCard(cv);
    PaintHistory(cv);
    PaintChat(cv);
    m_backBtn.Paint(cv);
}

void RoomView::PaintTimerCard(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.20f) / 0.6f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 16.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_timerCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_timerCard, shape::kEdge, pal.rule, shape::kHair);
    cv.CornerTicks({ m_timerCard.left + 8.0f, m_timerCard.top + 8.0f,
                     m_timerCard.right - 8.0f, m_timerCard.bottom - 8.0f },
                   WithAlpha(pal.ink300, 0.4f), 10.0f, 1.0f);

    float ix = m_timerCard.left + 26.0f;
    float right = m_timerCard.right - 26.0f;
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 10.5f; ts.letterSpacing = 2.4f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 专注计时", { ix, m_timerCard.top + 22.0f, right - 130.0f, m_timerCard.top + 38.0f }, ts, pal.ink300);
    cv.PerforationH(ix + 150.0f, right, m_timerCard.top + 30.0f, WithAlpha(pal.ruleStrong, 0.5f));
    // P1-5 #71 联动开关 + 白名单入口（计时卡右上）
    m_wlBtn.Paint(cv);
    m_linkBtn.Paint(cv);

    // 倒计时大数字
    int total = (int)std::ceil(m_remain);
    int mm = total / 60, ss = total % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", mm, ss);
    float frac = m_presetMin > 0 ? 1.0f - (m_remain / (float)(m_presetMin * 60)) : 0.0f;
    frac = Clamp01(frac);

    TextStyle ds; ds.role = FontRole::Mono; ds.size = 58.0f; ds.weight = DWRITE_FONT_WEIGHT_BOLD;
    ds.tabularNums = true; ds.vAlign = VAlign::Middle;
    D2D1_COLOR_F clockColor = (m_timer == TimerState::Running)
        ? (m_focusLost ? pal.brass : pal.seal)
        : pal.ink900;
    cv.Text(buf, { ix, m_countdownY, right, m_countdownY + 72.0f }, ds, clockColor);

    // 进度细条
    float barY = m_countdownY + 80.0f;
    float barW = right - ix;
    cv.FillRoundRect({ ix, barY, ix + barW, barY + 6.0f }, 3.0f, WithAlpha(pal.ink300, 0.20f));
    if (frac > 0.001f)
        cv.FillRoundRect({ ix, barY, ix + barW * frac, barY + 6.0f }, 3.0f,
                         m_timer == TimerState::Paused ? pal.brass : pal.seal);

    // P1-5 #71 前台学习联动状态条（进度条下方）
    PaintLinkStatus(cv, ix, barY + 16.0f, right);

    // 预设标签 + 按钮
    TextStyle ps; ps.role = FontRole::Mono; ps.size = 10.0f; ps.letterSpacing = 1.6f;
    cv.Text(L"预设（分钟）", { ix, m_presetY - 18.0f, right, m_presetY - 4.0f }, ps, pal.ink300);
    for (auto& b : m_presetBtns) b.Paint(cv);

    // 步进器
    TextStyle sts; sts.role = FontRole::Mono; sts.size = 10.0f; sts.letterSpacing = 1.6f;
    cv.Text(L"时长", { ix, m_stepperY - 18.0f, right, m_stepperY - 4.0f }, sts, pal.ink300);
    m_stepperMinus.Paint(cv);
    m_stepperPlus.Paint(cv);
    float stepW = 44.0f, valW = 120.0f;
    float stepX = ix + (right - ix - (stepW + 10.0f + valW + 10.0f + stepW)) * 0.5f;
    cv.FillRoundRect({ stepX + stepW + 10.0f, m_stepperY,
                       stepX + stepW + 10.0f + valW, m_stepperY + 40.0f },
                     shape::kEdgeSoft, WithAlpha(pal.rule, 0.5f));
    wchar_t vb[16];
    swprintf_s(vb, L"%d 分钟", m_presetMin);
    TextStyle vt; vt.role = FontRole::Mono; vt.size = 14.0f; vt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    vt.hAlign = HAlign::Center; vt.vAlign = VAlign::Middle;
    cv.Text(vb, { stepX + stepW + 10.0f, m_stepperY, stepX + stepW + 10.0f + valW, m_stepperY + 40.0f },
            vt, pal.ink700);

    // 控制按钮
    m_startBtn.Paint(cv);
    m_resetBtn.Paint(cv);

    // 当前安排 + 统计
    PaintArrangement(cv);
    // 背景音乐占位
    PaintMusic(cv);

    cv.PopOpacity();
    cv.PopTransform();
}

void RoomView::PaintLinkStatus(Canvas& cv, float x, float y, float right)
{
    const auto& pal = cv.Pal();

    // 取前台进程展示名（芙洛理自身显示为「芙洛理」）
    auto ProcName = [](const std::wstring& p) -> std::wstring {
        if (p == L"Flori") return L"芙洛理";
        return p.empty() ? L"桌面" : p;
    };

    D2D1_COLOR_F dot = pal.ink300;
    std::wstring msg;

    if (!FocusTracker::Instance().Running()) {
        msg = L"前台学习检测未启用（访客/截图模式）";
    } else if (!m_linkFocus) {
        msg = L"前台联动已关闭 · 计时不受前台影响";
    } else {
        FocusState st = FocusTracker::Instance().Snapshot();
        // #71 名单非空 = 只认名单；名单为空 = 内置智能判定，措辞要对应，否则用户看不懂为何暂停
        bool byList = !m_wlApps.empty();
        if (m_timer == TimerState::Running) {
            if (m_focusLost) {
                dot = pal.brass;
                msg = byList
                    ? (L"「" + ProcName(st.process) + L"」不在白名单 · 已暂停")
                    : (L"离开学习 · 番茄钟已暂停（" + ProcName(st.process) + L"）");
            } else {
                dot = pal.jade;
                msg = byList ? (L"名单内应用「" + ProcName(st.process) + L"」· 计时进行")
                             : std::wstring(L"前台专注中 · 计时进行");
            }
        } else {
            // 空闲态：实时预览联动将如何判定
            if (st.studying) {
                dot = pal.jade;
                msg = L"前台：" + ProcName(st.process) + (byList ? L" · 在名单内" : L" · 专注中");
            } else {
                dot = pal.ink300;
                msg = L"前台：" + ProcName(st.process) + (byList ? L" · 不在名单（将暂停）" : L" · 空闲/其他");
            }
        }
    }

    cv.FillCircle(x + 5.0f, y + 6.0f, 4.0f, dot);
    TextStyle ss; ss.role = FontRole::Sans; ss.size = 12.0f; ss.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ss.vAlign = VAlign::Middle;
    cv.Text(msg, { x + 16.0f, y, right, y + 14.0f }, ss, pal.ink700);
}

void RoomView::PaintMembersCard(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.28f) / 0.6f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 16.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_membersCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_membersCard, shape::kEdge, pal.rule, shape::kHair);
    cv.CornerTicks({ m_membersCard.left + 8.0f, m_membersCard.top + 8.0f,
                     m_membersCard.right - 8.0f, m_membersCard.bottom - 8.0f },
                   WithAlpha(pal.ink300, 0.4f), 10.0f, 1.0f);

    float ix = m_membersCard.left + 22.0f;
    float right = m_membersCard.right - 22.0f;
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 10.5f; ts.letterSpacing = 2.0f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(m_netLive ? L"SECTION · 在场成员（实时）" : L"SECTION · 在场成员（离线）",
            { ix, m_membersCard.top + 20.0f, right, m_membersCard.top + 36.0f }, ts,
            m_netLive ? pal.jade : pal.ink300);

    float my = m_membersCard.top + 52.0f;
    // 自己始终置顶
    cv.FillCircle(ix + 12.0f, my + 12.0f, 7.0f, pal.jade);
    TextStyle ns; ns.role = FontRole::Sans; ns.size = 13.0f; ns.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ns.vAlign = VAlign::Middle;
    cv.Text(L"你（我）", { ix + 28.0f, my, right, my + 24.0f }, ns, pal.ink900);
    TextStyle stt; stt.role = FontRole::Mono; stt.size = 10.0f; stt.vAlign = VAlign::Middle;
    cv.Text(m_timer == TimerState::Running ? L"专注中" : (m_timer == TimerState::Paused ? L"暂停" : L"在线"),
            { right - 60.0f, my, right, my + 24.0f }, stt,
            m_timer == TimerState::Running ? pal.seal : pal.jade);
    my += 28.0f;

    // 其他在线成员（来自服务端 presence 广播）
    for (const auto& mem : m_members) {
        if (my + 24.0f > m_membersCard.bottom - 8.0f) break;
        cv.FillCircle(ix + 12.0f, my + 12.0f, 5.0f, WithAlpha(pal.ink300, 0.5f));
        TextStyle mns; mns.role = FontRole::Sans; mns.size = 12.5f; mns.vAlign = VAlign::Middle;
        cv.Text(mem.name, { ix + 28.0f, my, right - 60.0f, my + 24.0f }, mns, pal.ink700);
        std::wstring st = mem.status == L"focusing" ? L"专注中" :
                          (mem.status == L"paused" ? L"暂停" : L"在线");
        cv.Text(st, { right - 60.0f, my, right, my + 24.0f }, stt,
                mem.status == L"focusing" ? pal.seal : pal.ink500);
        my += 28.0f;
    }

    if (m_members.empty() && !m_netLive) {
        TextStyle nt; nt.role = FontRole::Sans; nt.size = 11.5f; nt.vAlign = VAlign::Top;
        cv.Text(L"未连上服务端，当前为单机预览。启动服务端后自动恢复实时。",
                { ix, my + 4.0f, right, m_membersCard.bottom - 16.0f }, nt, pal.ink500);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void RoomView::PaintArrangement(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float ix = m_timerCard.left + 26.0f;
    float right = m_timerCard.right - 26.0f;

    TextStyle ls; ls.role = FontRole::Mono; ls.size = 10.5f; ls.letterSpacing = 2.0f;
    ls.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"当前时段 · 专注建议", { ix, m_arrY, right, m_arrY + 16.0f }, ls, pal.ink300);
    cv.PerforationH(ix + 150.0f, right, m_arrY + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    std::wstring arr = CurrentArrangement();
    TextStyle at; at.role = FontRole::Sans; at.size = 15.0f; at.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(arr, { ix, m_arrY + 22.0f, right, m_arrY + 48.0f }, at, pal.ink900);

    // 当前时段详情：时段（何时做）+ 完成标准（怎么做），一目了然
    int idx = CurrentArrangementIndex();
    if (idx >= 0 && (size_t)idx < m_todayItems.size()) {
        const auto& it = m_todayItems[idx];
        TextStyle dt; dt.role = FontRole::Sans; dt.size = 12.5f; dt.letterSpacing = 0.4f;
        cv.Text(L"时段 " + it.slot + L" · " + it.standard,
                 { ix, m_arrY + 49.0f, right, m_arrY + 63.0f }, dt,
                 WithAlpha(pal.ink500, 0.95f));
    }

    wchar_t buf[64];
    swprintf_s(buf, L"今日已专注 %d 分钟 · 累计 %d 次", m_todayMin, m_totalCount);
    TextStyle stt; stt.role = FontRole::Sans; stt.size = 13.0f; stt.letterSpacing = 0.6f;
    cv.Text(buf, { ix, m_statY, right, m_statY + 22.0f }, stt, pal.ink700);
}

void RoomView::PaintMusic(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float ix = m_timerCard.left + 26.0f;
    float right = m_timerCard.right - 26.0f;

    TextStyle hs; hs.role = FontRole::Mono; hs.size = 10.5f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(m_netLive ? L"SECTION · 背景音乐（实时）" : L"SECTION · 背景音乐（离线）",
            { ix, m_musicY, right, m_musicY + 16.0f }, hs, m_netLive ? pal.jade : pal.ink300);
    cv.PerforationH(ix + 170.0f, right, m_musicY + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    D2D1_RECT_F note{ ix, m_musicY + 22.0f, right, m_musicY + 22.0f + 64.0f };
    cv.FillRoundRect(note, shape::kEdgeSoft, WithAlpha(pal.brassWash, pal.dark ? 0.5f : 0.7f));
    cv.StrokeRoundRect(note, shape::kEdgeSoft, WithAlpha(pal.brass, 0.6f), shape::kHair);
    TextStyle nt; nt.role = FontRole::Sans; nt.size = 12.5f; nt.vAlign = VAlign::Middle;

    // #28 播放控制（note 右侧）：文本区域让开按钮 + 音量条
    float btnL = m_volRect.left - 12.0f;
    D2D1_RECT_F txtBox{ note.left + 14.0f, note.top, btnL, note.bottom };
    // #49 矢量播放/暂停图标（替代 ⏸ 表情蓝方块）
    PaintPlayPauseButton(cv, m_musicBtn.bounds,
                         MusicPlayer::Instance().GetState() == MusicPlayer::State::Playing,
                         m_musicBtn.HoverAmt() > 0.5f, m_musicBtn.PressAmt() > 0.5f, true);

    // 音量滑条（自绘）：滑槽 + 填充 + 手柄 + 音量百分比
    {
        float vol = MusicPlayer::Instance().GetVolume();
        float vp = (vol > 0.0f) ? sqrtf(vol) : 0.0f;   // 平方映射还原为线性位置
        const auto& vr = m_volRect;
        float trackY = (vr.top + vr.bottom) * 0.5f;
        // 槽
        cv.FillRoundRect({ vr.left, trackY - 2.0f, vr.right, trackY + 2.0f }, 2.0f, WithAlpha(pal.rule, 0.5f));
        // 填充（黄铜色）
        float fx = vr.left + (vr.right - vr.left) * vp;
        if (fx > vr.left + 2.0f)
            cv.FillRoundRect({ vr.left, trackY - 2.0f, fx, trackY + 2.0f }, 2.0f, pal.brass);
        // 手柄
        cv.FillCircle(fx, trackY, 5.0f, m_volDrag ? pal.brass : pal.ink300);
        cv.StrokeCircle(fx, trackY, 5.0f, pal.brass, 1.5f);
        // 「音量」标签 + 百分比
        TextStyle vs; vs.role = FontRole::Mono; vs.size = 10.0f; vs.vAlign = VAlign::Middle;
        cv.Text(L"音量", { vr.left, vr.top - 18.0f, vr.left + 40.0f, vr.top - 4.0f }, vs, pal.ink500);
        wchar_t vb[16];
        swprintf_s(vb, L"%d%%", (int)(vol * 100.0f + 0.5f));
        TextStyle pct; pct.role = FontRole::Mono; pct.size = 10.0f; pct.hAlign = HAlign::Right; pct.vAlign = VAlign::Middle;
        cv.Text(vb, { vr.left, vr.bottom + 4.0f, vr.right, vr.bottom + 18.0f }, pct, pal.ink300);
    }

    // 播放状态：优先展示播放器实时状态
    auto ps = MusicPlayer::Instance().GetState();
    if (ps == MusicPlayer::State::Loading) {
        cv.Text(L"正在加载曲目…", txtBox, nt, pal.ink700);
    } else if (ps == MusicPlayer::State::Playing || ps == MusicPlayer::State::Paused) {
        std::wstring t = MusicPlayer::Instance().GetTitle();
        if (t.empty()) t = m_track;
        cv.Text((ps == MusicPlayer::State::Playing ? L"正在播放：" : L"已暂停：") + t,
                txtBox, nt, pal.ink900);
    } else if (ps == MusicPlayer::State::Error) {
        cv.Text(L"播放失败：未连上服务端或音频解码出错。", txtBox, nt, pal.ink500);
    } else if (m_hasTrack && !m_track.empty()) {
        cv.Text(L"房间曲目：" + m_track, txtBox, nt, pal.ink900);
    } else if (m_hasMusic && !m_music.empty()) {
        std::wstring list = L"可用曲轨：" + m_music[0].second;
        for (size_t i = 1; i < m_music.size() && i < 4; ++i) list += L" · " + m_music[i].second;
        cv.Text(list, txtBox, nt, pal.ink700);
    } else {
        cv.Text(m_netLive ? L"暂无曲轨。管理员可在网页端设置房间背景音乐。" :
                L"未连上服务端，无法播放背景音乐。",
                txtBox, nt, pal.ink500);
    }
}

void RoomView::PaintHistory(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.40f) / 0.6f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_histCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_histCard, shape::kEdge, pal.rule, shape::kHair);

    float ix = m_histCard.left + 26.0f;
    float right = m_histCard.right - 26.0f;
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 10.5f; ts.letterSpacing = 2.0f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 近 30 条专注记录", { ix, m_histCard.top + 20.0f, right, m_histCard.top + 36.0f }, ts, pal.ink300);

    if (m_history.empty()) {
        TextStyle et; et.role = FontRole::Sans; et.size = 12.5f; et.vAlign = VAlign::Middle;
        cv.Text(L"还没有专注记录。开始一次专注，时长会自动计入档案。",
                { ix, m_histY, right, m_histCard.bottom - 16.0f }, et, pal.ink500);
    } else {
        int rows = (std::min)((int)m_history.size(), (int)((m_histCard.bottom - m_histY - 8.0f) / 30.0f));
        for (int i = 0; i < rows; ++i) {
            const auto& s = m_history[i];
            float ry = m_histY + (float)i * 30.0f;
            TextStyle rs; rs.role = FontRole::Mono; rs.size = 11.5f; rs.tabularNums = true; rs.vAlign = VAlign::Middle;
            cv.Text(s.date + L" " + FmtClock(s.start), { ix, ry, ix + 220.0f, ry + 26.0f }, rs, pal.ink700);
            wchar_t rb[48];
            swprintf_s(rb, L"%s · %d 分钟", s.tag.c_str(), s.min);
            TextStyle rt; rt.role = FontRole::Sans; rt.size = 11.5f; rt.vAlign = VAlign::Middle; rt.hAlign = HAlign::Right;
            cv.Text(rb, { right - 220.0f, ry, right, ry + 26.0f }, rt, pal.ink500);
            if (i < rows - 1) cv.Line(ix, ry + 27.0f, right, ry + 27.0f, WithAlpha(pal.rule, 0.5f), 1.0f);
        }
    }
    cv.PopOpacity();
    cv.PopTransform();
}

void RoomView::PaintChat(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.50f) / 0.6f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_chatCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_chatCard, shape::kEdge, pal.rule, shape::kHair);

    float ix = m_chatCard.left + 26.0f;
    float right = m_chatCard.right - 26.0f;
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 10.5f; ts.letterSpacing = 2.0f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    bool muted = (m_room == L"silent");
    cv.Text(muted ? L"SECTION · 公共聊天（本房禁言）" :
            (m_netLive ? L"SECTION · 公共聊天（实时）" : L"SECTION · 公共聊天（离线）"),
            { ix, m_chatCard.top + 20.0f, right, m_chatCard.top + 36.0f }, ts,
            muted ? pal.brass : (m_netLive ? pal.jade : pal.ink300));

    // 消息列表
    float my = m_chatY;
    int chatRows = (std::min)((int)m_chat.size(), 8);
    int start = (int)m_chat.size() - chatRows;
    if (start < 0) start = 0;
    for (int i = start; i < (int)m_chat.size(); ++i) {
        const auto& c = m_chat[i];
        TextStyle us; us.role = FontRole::Sans; us.size = 12.0f; us.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        us.vAlign = VAlign::Middle;
        cv.Text(c.user, { ix, my, ix + 120.0f, my + 24.0f }, us, pal.ink700);
        TextStyle tt; tt.role = FontRole::Sans; tt.size = 12.0f; tt.vAlign = VAlign::Middle;
        cv.Text(c.text, { ix + 128.0f, my, right, my + 24.0f }, tt, pal.ink900);
        my += 24.0f;
    }
    if (m_chat.empty()) {
        TextStyle et; et.role = FontRole::Sans; et.size = 12.0f; et.vAlign = VAlign::Middle;
        cv.Text(m_netLive ? L"还没有人发言，说点什么吧。" : L"未连上服务端，聊天不可用。",
                { ix, my, right, my + 24.0f }, et, pal.ink500);
        my += 24.0f;
    }

    // 输入框 + 发送按钮（静音房不显示）
    if (!muted) {
        float inpY = my + 6.0f;
        D2D1_RECT_F inp{ ix, inpY, right - 90.0f, inpY + 40.0f };
        cv.FillRoundRect(inp, shape::kEdgeSoft, m_chatEdit ? WithAlpha(pal.paperHi, 1.0f) : WithAlpha(pal.rule, 0.3f));
        cv.StrokeRoundRect(inp, shape::kEdgeSoft, m_chatEdit ? pal.jade : WithAlpha(pal.rule, 0.8f),
                           m_chatEdit ? shape::kStroke : shape::kHair);
        TextStyle pt; pt.role = FontRole::Sans; pt.size = 12.5f; pt.vAlign = VAlign::Middle;
        D2D1_RECT_F txt{ inp.left + 12.0f, inp.top, inp.right - 12.0f, inp.bottom };
        if (m_compose.empty() && !m_chatEdit && m_imeComp.empty()) {
            cv.Text(L"说点什么…（同自习室可见）", txt, pt, pal.ink300);
        } else if (m_chatEdit) {
            if (!m_imeComp.empty()) {
                // 组合中：已提交文本 + 未提交拼音（虚线下划线）一并自绘，
                // 并把组合串起点记为候选窗锚点（文档坐标 DIP）。
                m_imeAnchorX = PaintFieldEditIme(cv, txt, pt, m_compose,
                                                 EditCaretPos(m_chatEditHwnd),
                                                 m_imeComp, m_imeCompCaret,
                                                 pal.ink900, 0.0f);
            } else {
                // 编辑态：文字与光标由 PaintFieldEdit 绘制（光标位置取自隐藏代理）
                int sa = -1, sb = -1;
                lj::EditSelRange(m_chatEditHwnd, sa, sb);
                PaintFieldEdit(cv, txt, pt, m_compose, pal.ink900, EditCaretPos(m_chatEditHwnd), 0.0f, sa, sb);
                // 无组合时锚点跟着插入点走，下次起合成即从此处弹候选
                int cp = EditCaretPos(m_chatEditHwnd);
                if (cp < 0 || cp >(int)m_compose.size()) cp = (int)m_compose.size();
                m_imeAnchorX = txt.left + cv.MeasureWidth(m_compose.substr(0, (size_t)cp), pt);
            }
        } else {
            cv.Text(m_compose, txt, pt, pal.ink900);
        }

        m_sendBtn.Paint(cv);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void RoomView::PaintFocusOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = m_overlayA.value;
    cv.FillRect(m_area, WithAlpha(pal.dark ? pal.paperDeep : pal.paperLo, 0.94f * a));
    cv.PushOpacity(a);

    float cx = (m_area.left + m_area.right) * 0.5f;
    float cy = m_area.top + (m_area.bottom - m_area.top) * 0.38f;

    TextStyle st; st.role = FontRole::Mono; st.size = 12.0f; st.letterSpacing = 3.0f; st.hAlign = HAlign::Center;
    cv.Text(m_timer == TimerState::Paused ? L"已 暂 停 · 专 注 中" : L"专 注 中 · 界 面 已 静 默",
            { m_area.left, cy - 150.0f, m_area.right, cy - 126.0f }, st, pal.seal);

    int total = (int)std::ceil(m_remain);
    int mm = total / 60, ss = total % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", mm, ss);
    TextStyle ds; ds.role = FontRole::Mono; ds.size = 96.0f; ds.weight = DWRITE_FONT_WEIGHT_BOLD;
    ds.tabularNums = true; ds.hAlign = HAlign::Center; ds.vAlign = VAlign::Middle;
    cv.Text(buf, { m_area.left, cy - 100.0f, m_area.right, cy + 20.0f }, ds, pal.ink900);

    TextStyle at; at.role = FontRole::Sans; at.size = 14.0f; at.hAlign = HAlign::Center; at.vAlign = VAlign::Middle;
    cv.Text(L"当前安排：" + CurrentArrangement(), { m_area.left, cy + 34.0f, m_area.right, cy + 60.0f }, at, pal.ink500);

    TextStyle ht; ht.role = FontRole::Sans; ht.size = 11.5f; ht.hAlign = HAlign::Center;
    cv.Text(L"点击下方按钮可暂停 / 取消本次专注", { m_area.left, cy + 70.0f, m_area.right, cy + 90.0f }, ht, pal.ink300);

    m_overlayPause.Paint(cv);
    m_overlayCancel.Paint(cv);

    cv.PopOpacity();
}

// ============================================================
//  §3 云端实时：下行处理器（后台线程回调，只写受锁缓冲 + 置 m_netDirty）
// ============================================================
void RoomView::OnNetPresence(const lj::json::JVal& m)
{
    const auto* members = lj::json::JGet(m, "members");
    if (!members || !members->IsArr()) return;
    std::lock_guard<std::mutex> lk(m_netMu);
    m_srcMembers.clear();
    for (const auto& v : members->arr) {
        NetMember nm;
        nm.uid = net::FromUtf8(lj::json::JStr(v, "uid"));
        nm.name = net::FromUtf8(lj::json::JStr(v, "name"));
        nm.status = net::FromUtf8(lj::json::JStr(v, "status"));
        nm.focusContent = net::FromUtf8(lj::json::JStr(v, "focusContent"));
        const auto* fs = lj::json::JGet(v, "focusSeconds");
        if (fs) nm.focusSeconds = (int)fs->num;
        const auto* sn = lj::json::JGet(v, "since");
        if (sn) nm.since = (long long)sn->num;
        m_srcMembers.push_back(std::move(nm));
    }
    m_srcHasMembers = true;
    g_studyRoomOnline = (int)m_srcMembers.size();   // 供 F-D2 浮层读取
    m_netDirty.store(true);
}

void RoomView::OnNetChatMsg(const lj::json::JVal& m)
{
    const auto* msg = lj::json::JGet(m, "msg");
    if (!msg || !msg->IsObj()) return;
    NetChat nc;
    nc.id = net::FromUtf8(lj::json::JStr(*msg, "id"));
    nc.user = net::FromUtf8(lj::json::JStr(*msg, "user"));
    nc.uid = net::FromUtf8(lj::json::JStr(*msg, "uid"));
    nc.text = net::FromUtf8(lj::json::JStr(*msg, "text"));
    const auto* ts = lj::json::JGet(*msg, "ts");
    if (ts) nc.ts = (long long)ts->num;
    {
        std::lock_guard<std::mutex> lk(m_netMu);
        m_srcChat.push_back(std::move(nc));
        if (m_srcChat.size() > 100) m_srcChat.erase(m_srcChat.begin(), m_srcChat.end() - 100);
    }
    m_netDirty.store(true);
}

void RoomView::OnNetChatHistory(const lj::json::JVal& m)
{
    const auto* list = lj::json::JGet(m, "list");
    if (!list || !list->IsArr()) return;
    std::lock_guard<std::mutex> lk(m_netMu);
    m_srcChat.clear();
    for (const auto& v : list->arr) {
        NetChat nc;
        nc.id = net::FromUtf8(lj::json::JStr(v, "id"));
        nc.user = net::FromUtf8(lj::json::JStr(v, "user"));
        nc.uid = net::FromUtf8(lj::json::JStr(v, "uid"));
        nc.text = net::FromUtf8(lj::json::JStr(v, "text"));
        const auto* ts = lj::json::JGet(v, "ts");
        if (ts) nc.ts = (long long)ts->num;
        m_srcChat.push_back(std::move(nc));
    }
    m_netDirty.store(true);
}

void RoomView::OnNetMusicMeta(const lj::json::JVal& m)
{
    std::lock_guard<std::mutex> lk(m_netMu);
    m_srcTrack = net::FromUtf8(lj::json::JStr(m, "track"));
    m_srcHasTrack = true;
    m_netDirty.store(true);
}

void RoomView::OnNetMusicList(const lj::json::JVal& m)
{
    const auto* tracks = lj::json::JGet(m, "tracks");
    if (!tracks || !tracks->IsArr()) return;
    std::lock_guard<std::mutex> lk(m_netMu);
    m_srcMusic.clear();
    for (const auto& v : tracks->arr) {
        // #28 需要 id 才能播放；title 供显示/匹配 meta
        std::wstring id = net::FromUtf8(lj::json::JStr(v, "id"));
        std::wstring title = net::FromUtf8(lj::json::JStr(v, "title"));
        if (!id.empty() || !title.empty()) m_srcMusic.emplace_back(std::move(id), std::move(title));
    }
    m_srcHasMusic = true;
    m_netDirty.store(true);
}

void RoomView::OnNetAuthOk(const lj::json::JVal&)
{
    // 连上 / 重连成功 → 重新进房 + 拉音乐 + 发在场
    auto rt = &Realtime::Instance();
    rt->JoinRoom(net::ToUtf8(m_room));
    rt->RequestMusic();
    SendMyPresence();
}

void RoomView::PlayMusic()
{
    auto ps = MusicPlayer::Instance().GetState();
    if (ps == MusicPlayer::State::Playing || ps == MusicPlayer::State::Paused) {
        MusicPlayer::Instance().Toggle();     // 暂停 / 继续
        return;
    }
    // 空闲或出错 → 重新选曲播放
    std::wstring id, title;
    if (m_hasTrack && !m_track.empty()) {
        // 房间管理员设置的曲目（meta 是标题）→ 在曲目表里按标题匹配 id
        for (const auto& t : m_music) {
            if (t.second == m_track) { id = t.first; title = t.second; break; }
        }
    }
    if (id.empty() && !m_music.empty()) { id = m_music[0].first; title = m_music[0].second; }

    // P1-3：无云端曲目时，兜底播放 assets/media/music 里的第一首本地音乐
    if (id.empty()) {
        std::wstring local = FirstLocalMusic();
        if (!local.empty()) {
            size_t p = local.find_last_of(L"\\/");
            title = (p == std::wstring::npos) ? local : local.substr(p + 1);
            p = title.find_last_of(L'.');
            if (p != std::wstring::npos) title = title.substr(0, p);
            MusicPlayer::Instance().PlayLocal(local, title);
            return;
        }
    }
    if (id.empty()) return;
    MusicPlayer::Instance().Play(id, title);
}

std::wstring RoomView::FirstLocalMusic()
{
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exe(buf, n);
    size_t pos = exe.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"" : exe.substr(0, pos);
    std::wstring root = dir + L"\\assets\\media\\music";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring first;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name(fd.cFileName);
        std::wstring ext;
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            ext = name.substr(dot);
            for (auto& c : ext) c = (wchar_t)std::towlower(c);
        }
        if (ext == L".mp3" || ext == L".wav" || ext == L".ogg" || ext == L".flac" ||
            ext == L".m4a" || ext == L".aac" || ext == L".wma") {
            first = root + L"\\" + name;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return first;
}

void RoomView::MusicVolumeFromX(float x)
{
    const auto& vr = m_volRect;
    if (vr.right <= vr.left) return;
    float t = (x - vr.left) / (vr.right - vr.left);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    // 用平方曲线：低音量区间更细腻（人耳对响度近似对数感知）
    MusicPlayer::Instance().SetVolume(t * t);
}

void RoomView::SendMyPresence()
{
    std::string status, focusContent;
    int focusSeconds = 0;
    if (m_timer == TimerState::Running) {
        status = "focusing";
        focusContent = net::ToUtf8(CurrentArrangement());
        if (m_focusStart > 0)
            focusSeconds = (int)((long long)std::time(nullptr) - m_focusStart);
    } else if (m_timer == TimerState::Paused) {
        status = "paused";
        focusContent = net::ToUtf8(CurrentArrangement());
    } else {
        status = "online";
    }
    Realtime::Instance().SendPresence(status, focusContent, focusSeconds);
}

void RoomView::SnapshotNet()
{
    std::lock_guard<std::mutex> lk(m_netMu);
    m_members = m_srcMembers;
    m_chat = m_srcChat;
    m_track = m_srcTrack;
    m_hasTrack = m_srcHasTrack;
    m_music = m_srcMusic;
    m_hasMusic = m_srcHasMusic;
    m_netLive = Realtime::Instance().Connected();
}

// ============================================================
//  辅助
// ============================================================
int RoomView::CurrentArrangementIndex() const
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    int nowMin = st.wHour * 60 + st.wMinute;
    int best = -1;
    for (size_t i = 0; i < m_todayItems.size(); ++i) {
        int s = ParseSlot(m_todayItems[i].slot);
        if (s <= nowMin) best = (int)i;
    }
    if (best >= 0) return best;
    for (size_t i = 0; i < m_todayItems.size(); ++i)
        if (ParseSlot(m_todayItems[i].slot) > nowMin) return (int)i;
    return -1;
}

std::wstring RoomView::CurrentArrangement() const
{
    int idx = CurrentArrangementIndex();
    if (idx >= 0 && (size_t)idx < m_todayItems.size()) return m_todayItems[idx].title;
    return L"自习";
}

std::wstring RoomView::FmtClock(long long epoch)
{
    if (epoch <= 0) return L"--:--";
    time_t t = (time_t)epoch;
    struct tm lt;
    localtime_s(&lt, &t);
    wchar_t b[16];
    swprintf_s(b, L"%02d:%02d", lt.tm_hour, lt.tm_min);
    return b;
}

int RoomView::ParseSlot(const std::wstring& hhmm)
{
    if (hhmm.size() < 5) return -1;
    int h = _wtoi(hhmm.substr(0, 2).c_str());
    int m = _wtoi(hhmm.substr(3, 2).c_str());
    return h * 60 + m;
}

} // namespace lj
