#include "views/RoadmapView.h"
#include "app/Store.h"
#include "core/Hwnd.h"
#include "ui/FieldText.h"
#include "ui/Layout.h"
#include <richedit.h>    // RichEdit（RICHEDIT50W）：正文富文本编辑（加粗/字号/自动换行）
#include <commdlg.h>     // 插入图片：GetOpenFileNameW（comdlg32 已链接）
#include <fstream>
#include <cmath>
#include <algorithm>
#include <ctime>

#ifndef PFN_NUMBERED
#define PFN_NUMBERED 2   // 本 SDK 头文件未定义：有序列表（PFN_BULLET=1 已定义）
#endif
#ifndef SF_SELECTION
#define SF_SELECTION 0x0008   // EM_STREAMIN/OUT 作用于当前选区（本 SDK 头文件未定义）
#endif

namespace lj {

bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// 颜色浮层预设色板（兼顾字体色与背景色需求）
namespace {
const COLORREF kSwatchCols[] = {
    0x000000, 0x595959, 0xC00000, 0xED7D31, 0xFFC000,
    0x548235, 0x2E75B6, 0x7030A0, 0xD9D9D9, 0xFFFFFF
};
const int kSwatchN = (int)(sizeof(kSwatchCols) / sizeof(kSwatchCols[0]));
} // namespace

// ---------- RichEdit RTF 字节流 ↔ 内存字符串 ----------
namespace {
DWORD CALLBACK RtfInCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    auto* s = reinterpret_cast<std::string*>(cookie);
    LONG n = (LONG)std::min((size_t)cb, s->size());
    if (n > 0) { memcpy(buf, s->data(), (size_t)n); s->erase(0, (size_t)n); }
    *pcb = n;
    return 0;
}
DWORD CALLBACK RtfOutCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    auto* s = reinterpret_cast<std::string*>(cookie);
    s->append(reinterpret_cast<const char*>(buf), (size_t)cb);
    *pcb = cb;
    return 0;
}

// 从 RichEdit 取出 RTF 字节流（SF_RTF）
std::string EdGetRtf(HWND h)
{
    std::string out;
    if (!h) return out;
    EDITSTREAM es{};
    es.dwCookie = (DWORD_PTR)&out;
    es.pfnCallback = RtfOutCallback;
    SendMessageW(h, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    return out;
}
} // namespace

// ============================================================
//  生命周期
// ============================================================
void RoadmapView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    LoadColumns();
    m_srcFetched = false;
    m_pubLive = false;

    // §4 接入：注册 col:changed 实时事件 + 拉取公共专栏
    Realtime::Instance().On("col:changed", [this](const lj::json::JVal&) { OnColChanged({}); });
    FetchPublicColumns();
}

void RoadmapView::OnLeave()
{
    if (m_editing) CloseEditor(false);
    if (m_previewing) ClosePreview();
    if (m_pubPreviewing) ClosePubPreview();
    DestroyEditors();
    Realtime::Instance().Off("col:changed");
    View::OnLeave();
}

void RoadmapView::LoadColumns()
{
    m_cols = CheckinStore::Instance().LoadColumns();
    m_favs = CheckinStore::Instance().LoadFavorites();
}

bool RoadmapView::IsColFav(const std::wstring& id) const
{
    for (const auto& f : m_favs)
        if (f.kind == L"column" && f.id == id) return true;
    return false;
}

// ============================================================
//  布局
// ============================================================
void RoadmapView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_cvCached = &cv;
    LoadColumns();
    float availW = area.right - area.left;
    m_contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    m_x0 = area.left + (availW - m_contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(m_x0, area.top + 30.0f, m_contentW, 0.0f);
    m_contentTop = flow.cursorY;
    flow.block(96.0f);   // 标题区
    flow.block(8.0f);    // 标题区与列表的间距

    m_newBtn.label = L"+ 新建专栏";
    m_newBtn.fontSize = 13.0f;
    m_newBtn.tag = L"new";
    m_newBtn.bounds = { m_x0 + m_contentW - 168.0f, m_contentTop + 8.0f,
                        m_x0 + m_contentW, m_contentTop + 46.0f };
    m_newBtn.onClick = [this] { OpenEditor(L""); };

    m_backBtn.label = L"返 回 首 页";
    m_backBtn.tag = L"00";
    m_backBtn.fontSize = 13.0f;

    m_listY = flow.cursorY;
    RelayoutList();

    m_widgets.clear();
    m_widgets.push_back(&m_newBtn);
    m_widgets.push_back(&m_backBtn);
}

void RoadmapView::RelayoutList()
{
    m_cardRects.clear();
    m_editBtns.clear();
    m_delBtns.clear();
    m_viewBtns.clear();   // 曾漏清空 → 重排后仍沿用首次布局的陈旧矩形，「查看」点不中
    m_favBtns.clear();
    float gap = 16.0f;
    // 卡片高度：标题区 + 元信息 + 正文预览（约 8-10 行）+ 底部留白
    // 原 132px 太矮，长文（如总线路图种子）被严重截断
    const float cardH = 220.0f;
    const float x0 = m_x0, contentW = m_contentW;
    lj::ui::VLayout flow(x0, m_listY, contentW, 0.0f);
    // 空态卡片矩形（此前从未赋值，恒为 {0,0,0,0}：PaintEmpty 拿着退化矩形画
    // 「空白」签和两行文案，D2D 对负宽矩形产出未定义行为 → 窗口左上角出现
    // 一坨被切碎的字形残迹）。无论列表是否为空都给一个合法矩形，仅空态时使用。
    m_emptyRect = { x0, m_listY, x0 + contentW, m_listY + 190.0f };
    for (size_t i = 0; i < m_cols.size(); ++i) {
        float top = flow.block(cardH + gap).top;
        D2D1_RECT_F r{ x0, top, x0 + contentW, top + cardH };
        m_cardRects.push_back(r);
        // 右上四枚操作丸（自右向左）：删除 / 编辑 / 查看 / 收藏
        float bw = 54.0f, bwFav = 70.0f, bh = 28.0f, pad = 8.0f;
        float R = r.right - 12.0f, T = r.top + 14.0f, B = r.top + 14.0f + bh;
        float xDel  = R - bw;
        float xEdit = xDel - pad - bw;
        float xView = xEdit - pad - bw;
        float xFav  = xView - pad - bwFav;
        m_favBtns.push_back({  xFav,  T, xFav + bwFav, B });
        m_viewBtns.push_back({ xView, T, xView + bw,   B });
        m_editBtns.push_back({ xEdit, T, xEdit + bw,   B });
        m_delBtns.push_back({  xDel,  T, R,            B });
    }
    if (m_cols.empty())
        flow.block(190.0f + 16.0f);   // 空态卡占位：否则后续「返回首页」/公共专栏区与卡片重叠
    {
        float top = flow.block(46.0f + 30.0f).top;
        m_backBtn.bounds = { x0, top, x0 + 150.0f, top + 46.0f };
    }

    // §4 公共专栏区（服务端）
    m_pubTitleY = flow.block(76.0f).top;
    m_pubListY = flow.cursorY;
    m_pubCardRects.clear();
    m_pubLikeBtns.clear();
    float pubCardH = 104.0f, pubGap = 12.0f;
    if (m_pubPosts.empty()) {
        flow.block(80.0f);   // 空状态占位
    } else {
        int pubRows = (std::min)((int)m_pubPosts.size(), 30);
        for (int i = 0; i < pubRows; ++i) {
            float top = flow.block(pubCardH + pubGap).top;
            D2D1_RECT_F r{ x0, top, x0 + contentW, top + pubCardH };
            m_pubCardRects.push_back(r);
            m_pubLikeBtns.push_back({ r.right - 84.0f, r.bottom - 34.0f, r.right - 14.0f, r.bottom - 10.0f });
        }
    }
    flow.block(20.0f);
    SetContentHeight(flow.cursorY - m_area.top);
}

void RoadmapView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;

    // 编辑器弹层淡入淡出
    float target = m_editing ? 1.0f : 0.0f;
    m_editAnim += (target - m_editAnim) * (std::min)(1.0f, dt * 14.0f);
    if (!m_editing && m_editAnim < 0.004f) m_editAnim = 0.0f;

    // 预览弹层淡入淡出
    float ptarget = m_previewing ? 1.0f : 0.0f;
    m_previewAnim += (ptarget - m_previewAnim) * (std::min)(1.0f, dt * 14.0f);
    if (!m_previewing && m_previewAnim < 0.004f) m_previewAnim = 0.0f;

    if (m_editing) {
        // 标题编辑中：鼠标先交给标题框（点击定位光标 / 拖拽框选），点外部由失焦回调提交
        if (m_titleActive && m_cvCached) {
            TextStyle ts; ts.role = FontRole::Serif; ts.size = 17.0f;
            ts.weight = DWRITE_FONT_WEIGHT_BOLD; ts.vAlign = VAlign::Middle;
            m_edTitle.HandleMouse(in, *m_cvCached, m_edTitleBox, ts, 0.0f, 4.0f);
        }
        if (in.keyDown[VK_ESCAPE]) { m_edColorMode = 0; m_fmtBrush = false; CloseEditor(false); return; }
        if (in.clicked) {
            float mx = in.mouseX, my = in.mouseY;

            // 点标题框 → 进入标题编辑（1×1 透明代理 + D3D 自绘，不覆盖边框）
            if (Hit(m_edTitleBox, mx, my)) {
                if (!m_titleActive) {
                    m_edTitle.onEnter     = [this] { CommitTitleEdit(); if (m_edBody) SetFocus(m_edBody); };
                    m_edTitle.onEsc       = [this] { m_edColorMode = 0; m_fmtBrush = false; CloseEditor(false); };
                    m_edTitle.onKillFocus = [this] { CommitTitleEdit(); };
                    m_edTitle.Begin(m_edTitleBuf, false, 17.0f);
                    m_titleActive = true;
                }
                return;
            }

            // 颜色浮层优先：点色板即应用并关闭
            if (m_edColorMode != 0 && !m_edSwatches.empty()) {
                for (size_t i = 0; i < m_edSwatches.size(); ++i) {
                    if (Hit(m_edSwatches[i], mx, my)) {
                        EdSetColor(kSwatchCols[i], m_edColorMode == 2);
                        m_edColorMode = 0;
                        SetFocus(m_edBody);
                        return;
                    }
                }
            }

            // 格式工具栏：作用于 RichEdit 选区 / 插入点（无选区时作用于后续输入）
            if (Hit(m_edUndo, mx, my))      { EdUndo();  SetFocus(m_edBody); return; }
            if (Hit(m_edRedo, mx, my))      { EdRedo();  SetFocus(m_edBody); return; }
            if (Hit(m_edBrush, mx, my))     { EdFormatBrush(); return; }   // 两步：取→刷
            if (Hit(m_edClear, mx, my))     { EdClearFormat(); SetFocus(m_edBody); return; }
            if (Hit(m_edH1, mx, my))        { EdApplyHeading(1); SetFocus(m_edBody); return; }
            if (Hit(m_edH2, mx, my))        { EdApplyHeading(2); SetFocus(m_edBody); return; }
            if (Hit(m_edH3, mx, my))        { EdApplyHeading(3); SetFocus(m_edBody); return; }
            if (Hit(m_edBold, mx, my))      { EdApplyFormat(CFM_BOLD,       EdHasEffect(CFE_BOLD)       ? 0 : CFE_BOLD);       SetFocus(m_edBody); return; }
            if (Hit(m_edItalic, mx, my))    { EdApplyFormat(CFM_ITALIC,     EdHasEffect(CFE_ITALIC)     ? 0 : CFE_ITALIC);     SetFocus(m_edBody); return; }
            if (Hit(m_edUnderline, mx, my)) { EdApplyFormat(CFM_UNDERLINE,  EdHasEffect(CFE_UNDERLINE)  ? 0 : CFE_UNDERLINE);  SetFocus(m_edBody); return; }
            if (Hit(m_edStrike, mx, my))    { EdApplyFormat(CFM_STRIKEOUT,  EdHasEffect(CFE_STRIKEOUT)  ? 0 : CFE_STRIKEOUT);  SetFocus(m_edBody); return; }
            if (Hit(m_edSizeS, mx, my))     { EdApplyFormat(CFM_SIZE, 0, 180); SetFocus(m_edBody); return; } // 9pt
            if (Hit(m_edSizeM, mx, my))     { EdApplyFormat(CFM_SIZE, 0, 240); SetFocus(m_edBody); return; } // 12pt
            if (Hit(m_edSizeL, mx, my))     { EdApplyFormat(CFM_SIZE, 0, 320); SetFocus(m_edBody); return; } // 16pt
            if (Hit(m_edColor, mx, my))     { m_edColorMode = (m_edColorMode == 1) ? 0 : 1; return; }
            if (Hit(m_edBg, mx, my))        { m_edColorMode = (m_edColorMode == 2) ? 0 : 2; return; }
            if (Hit(m_edAlignL, mx, my))    { EdSetAlignment(PFA_LEFT);   SetFocus(m_edBody); return; }
            if (Hit(m_edAlignC, mx, my))    { EdSetAlignment(PFA_CENTER); SetFocus(m_edBody); return; }
            if (Hit(m_edAlignR, mx, my))    { EdSetAlignment(PFA_RIGHT);  SetFocus(m_edBody); return; }
            if (Hit(m_edOList, mx, my))     { EdToggleList(PFN_NUMBERED); SetFocus(m_edBody); return; }
            if (Hit(m_edUList, mx, my))     { EdToggleList(PFN_BULLET);   SetFocus(m_edBody); return; }
            if (Hit(m_edImage, mx, my))     { EdInsertImage(); return; }
            if (Hit(m_edLink, mx, my))      { EdInsertLink(); SetFocus(m_edBody); return; }
            if (Hit(m_edHr, mx, my))        { EdInsertDivider(); SetFocus(m_edBody); return; }
            if (Hit(m_edCancel, mx, my))    { m_edColorMode = 0; m_fmtBrush = false; CloseEditor(false); return; }
            if (Hit(m_edSave,   mx, my))    { m_edColorMode = 0; m_fmtBrush = false; CloseEditor(true);  return; }
            if (Hit(m_edPublish,mx, my))    { m_edColorMode = 0; m_fmtBrush = false; DoPublish();        return; }
            if (!m_editId.empty() && Hit(m_edDelete, mx, my)) { DeleteColumn(m_editId); return; }

            // 点工具栏/卡片外区域：收起颜色浮层（不关闭编辑器）
            if (m_edColorMode != 0) m_edColorMode = 0;
        }
        return;
    }

    if (m_previewing) {
        ComputePreviewRects();   // 保证本帧矩形与视口同步（如缩放窗口）
        if (in.keyDown[VK_ESCAPE]) { ClosePreview(); return; }
        // 滚轮在正文区内滚动正文（wheel 正=向下；向下应增大偏移以露出下方内容）
        if (in.wheel != 0.0f) {
            float wh = m_pvBody.bottom - m_pvBody.top;
            float contentH = m_pvContentH;
            float maxS = (std::max)(0.0f, contentH - wh);
            // 每格 48 DIP（原 0.5 太慢，爬行手感）
            m_previewScroll = (std::max)(0.0f, (std::min)(maxS, m_previewScroll + in.wheel * 48.0f));
        }
        if (in.clicked) {
            float mx = in.mouseX, my = in.mouseY;
            if (Hit(m_pvClose, mx, my)) { ClosePreview(); return; }
            if (Hit(m_pvEdit,  mx, my)) { std::wstring id = m_previewId; ClosePreview(); OpenEditor(id); return; }
            if (!m_previewId.empty() && Hit(m_pvDelete, mx, my)) { DeleteColumn(m_previewId); return; }
            if (!Hit(m_pvCard, mx, my)) { ClosePreview(); return; }   // 点卡外关闭
        }
        return;
    }

    // ---- §4 公共专栏详情弹层 ----
    if (m_pubPreviewing || m_pubPreviewAnim > 0.004f) {
        ComputePubPreviewRects();
        if (m_pubDetailDirty.exchange(false)) OnPubDetail(m_pubDetailResp);
        if (m_pubPreviewing) {
            if (in.keyDown[VK_ESCAPE]) { ClosePubPreview(); return; }
            if (in.wheel != 0.0f) {
                float wh = m_pubPvBody.bottom - m_pubPvBody.top;
                float maxS = (std::max)(0.0f, m_pubPvContentH - wh);
                m_pubPreviewScroll = (std::max)(0.0f, (std::min)(maxS, m_pubPreviewScroll + in.wheel * 48.0f));
                // 评论输入框随内容滚动（位置由 Layout/ComputePubPreviewRects 重算，代理在 Paint 自动跟随）
            }
            // 评论编辑中：鼠标先交给输入框（点击定位光标 / 拖拽框选）
            if (m_pubCommentEdit && m_cvCached) {
                TextStyle pt; pt.role = FontRole::Sans; pt.size = 12.5f; pt.vAlign = VAlign::Middle;
                D2D1_RECT_F tbox{ m_pubPvInput.left + 12.0f, m_pubPvInput.top,
                                  m_pubPvInput.right - 12.0f, m_pubPvInput.bottom };
                m_pubComment.HandleMouse(in, *m_cvCached, tbox, pt, 0.0f, 0.0f);
            }
            if (in.clicked) {
                float mx = in.mouseX, my = in.mouseY;
                if (Hit(m_pubPvClose, mx, my)) { ClosePubPreview(); return; }
                if (!Hit(m_pubPvCard, mx, my)) { ClosePubPreview(); return; }
                if (Hit(m_pubPvLike, mx, my)) {
                    std::string pid = m_pubPreviewId;
                    Cloud::RunAsync([this, pid] { bool on; int c; Cloud::Instance().LikeColumn(pid, on, c); FetchPublicColumns(); });
                    return;
                }
                if (Hit(m_pubPvFav, mx, my)) {
                    std::string pid = m_pubPreviewId;
                    Cloud::RunAsync([this, pid] { bool on; int c; Cloud::Instance().FavColumn(pid, on, c); FetchPublicColumns(); });
                    return;
                }
                if (Hit(m_pubPvInput, mx, my) && !m_pubCommentEdit) { BeginPubCommentEdit(); return; }
                if (Hit(m_pubPvSend, mx, my)) { EndPubCommentEdit(true); return; }
            }
        }
        return;
    }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // 悬浮卡片高亮
    m_hoverCard = -1;
    float cx = in.mouseX, cy = in.mouseY + ScrollY();
    for (size_t i = 0; i < m_cardRects.size(); ++i)
        if (Hit(m_cardRects[i], cx, cy)) { m_hoverCard = (int)i; break; }

    if (in.clicked) {
        float mx = in.mouseX, my = in.mouseY + ScrollY();
        if (m_hoverCard >= 0) {
            if (Hit(m_editBtns[m_hoverCard], mx, my)) { OpenEditor(m_cols[m_hoverCard].id); return; }
            if (Hit(m_delBtns[m_hoverCard],  mx, my)) { DeleteColumn(m_cols[m_hoverCard].id); return; }
            if (Hit(m_viewBtns[m_hoverCard], mx, my)) { OpenPreview(m_cols[m_hoverCard].id); return; }
            if (m_hoverCard < (int)m_favBtns.size() && Hit(m_favBtns[m_hoverCard], mx, my)) {
                const auto& c = m_cols[m_hoverCard];
                CheckinStore::Instance().ToggleFav(L"column", c.id, c.title, c.author);
                m_favs = CheckinStore::Instance().LoadFavorites();
                return;
            }
            OpenPreview(m_cols[m_hoverCard].id);   // 点卡片本身 → 预览（只读）
            return;
        }
    }

    // §4 云端快照消费
    if (m_cloudDirty.exchange(false)) SnapshotCloud();

    // 发布结果提示（后台线程置 m_pubResult 原子码，这里消费成 UI 提示）
    int pr = m_pubResult.exchange(0);
    if (pr) {
        m_pubHint = (pr == 1) ? L"已发布到公共区" : L"发布失败：未连上服务端，已保存到本地";
        m_pubHintUntil = (long long)time(nullptr) + 5;
    }

    // §4 公共专栏卡片悬浮 + 点赞
    m_hoverPub = -1;
    {
        float cx = in.mouseX, cy = in.mouseY + ScrollY();
        for (size_t i = 0; i < m_pubCardRects.size(); ++i)
            if (Hit(m_pubCardRects[i], cx, cy)) { m_hoverPub = (int)i; break; }
    }
    if (in.clicked && m_hoverPub >= 0 && m_hoverPub < (int)m_pubPosts.size()) {
        float mx = in.mouseX, my = in.mouseY + ScrollY();
        if (Hit(m_pubLikeBtns[m_hoverPub], mx, my)) {
            std::string pid = m_pubPosts[m_hoverPub].id;
            Cloud::RunAsync([this, pid] {
                bool on; int cnt;
                Cloud::Instance().LikeColumn(pid, on, cnt);
                FetchPublicColumns();
            });
        } else {
            // 点卡片非按钮区 → 打开详情弹层
            OpenPubPreview(m_pubPosts[m_hoverPub].id);
        }
    }
}

// ============================================================
//  绘制
// ============================================================
void RoadmapView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    if (m_cols.empty()) PaintEmpty(cv, x0, m_listY, contentW);
    else                  PaintList(cv, x0, m_listY, contentW);

    m_newBtn.Paint(cv);
    m_backBtn.Paint(cv);

    // §4 公共专栏（服务端）
    {
        float appear = Clamp01((m_t - 0.55f) / 0.6f);
        if (appear > 0.004f) {
            float e = ease::OutCubic(appear);
            cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
            cv.PushOpacity(e);

            // 分隔线 + 标题
            TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
            sec.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(m_pubLive ? L"SECTION · 公共专栏（实时）" : L"SECTION · 公共专栏（离线）",
                    { x0, m_pubTitleY, x0 + 420.0f, m_pubTitleY + 16.0f }, sec,
                    m_pubLive ? pal.jade : pal.ink300);
            TextStyle h2; h2.role = FontRole::Serif; h2.size = 22.0f; h2.weight = DWRITE_FONT_WEIGHT_BOLD;
            h2.letterSpacing = 1.5f;
            cv.Text(L"公共专栏", { x0, m_pubTitleY + 22.0f, x0 + contentW, m_pubTitleY + 54.0f }, h2, pal.ink900);
            // 发布结果提示（5 秒后消失）：成功绿 / 失败红
            if (m_pubHintUntil > (long long)time(nullptr) && !m_pubHint.empty()) {
                TextStyle ht; ht.role = FontRole::Sans; ht.size = 12.0f; ht.vAlign = VAlign::Middle; ht.hAlign = HAlign::Right;
                cv.Text(m_pubHint, { x0, m_pubTitleY + 22.0f, x0 + contentW - 6.0f, m_pubTitleY + 54.0f }, ht,
                        m_pubHint.find(L"失败") != std::wstring::npos ? pal.vermilion : pal.jade);
            }
            cv.PerforationH(x0, x0 + contentW, m_pubTitleY + 64.0f, WithAlpha(pal.ruleStrong, 0.5f));

            if (m_pubPosts.empty()) {
                TextStyle et; et.role = FontRole::Sans; et.size = 12.5f; et.vAlign = VAlign::Middle;
                cv.Text(m_pubLive ? L"还没有公共专栏，点「新建专栏」写一篇，再点「发布」上传到这里吧。" :
                        L"未连上服务端，公共专栏不可用。本地专栏仍可正常编辑。",
                        { x0, m_pubListY, x0 + contentW, m_pubListY + 40.0f }, et, pal.ink500);
            } else {
                for (size_t i = 0; i < m_pubPosts.size() && i < m_pubCardRects.size(); ++i) {
                    const auto& p = m_pubPosts[i];
                    const auto& r = m_pubCardRects[i];
                    bool hover = (m_hoverPub == (int)i);
                    cv.PaperCard(r, 0.0f, shape::kEdge);
                    cv.FillRect({ r.left, r.top, r.left + 3.0f, r.bottom }, WithAlpha(pal.brass, 0.7f));

                    TextStyle ts; ts.role = FontRole::Sans; ts.size = 14.5f; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
                    cv.Text(p.title, { r.left + 18.0f, r.top + 14.0f, r.right - 100.0f, r.top + 38.0f }, ts, pal.ink900);

                    TextStyle ms; ms.role = FontRole::Mono; ms.size = 10.5f;
                    std::wstring meta = p.author + L"  ·  " + FormatTs(p.createdAt);
                    if (p.mine) meta += L"  ·  我的";
                    cv.Text(meta, { r.left + 18.0f, r.top + 38.0f, r.right - 18.0f, r.top + 54.0f }, ms, pal.ink500);

                    TextStyle bs; bs.role = FontRole::Sans; bs.size = 11.5f; bs.vAlign = VAlign::Top;
                    cv.Text(p.excerpt, { r.left + 18.0f, r.top + 56.0f, r.right - 18.0f, r.bottom - 14.0f }, bs, pal.ink700);

                    // 点赞 / 收藏 / 评论数
                    TextStyle is; is.role = FontRole::Sans; is.size = 11.5f; is.vAlign = VAlign::Middle;
                    std::wstring info = (p.liked ? L"♥ " : L"♡ ") + std::to_wstring(p.likeCount) +
                                        L"    " + (p.faved ? L"★ " : L"☆ ") + std::to_wstring(p.favCount) +
                                        L"    💬 " + std::to_wstring(p.commentCount);
                    cv.Text(info, { r.left + 18.0f, r.bottom - 34.0f, r.right - 18.0f, r.bottom - 10.0f },
                            is, pal.ink500);
                }
            }

            cv.PopOpacity();
            cv.PopTransform();
        }
    }

    cv.PopTransform();
    cv.PopClip();

    // 公共专栏详情弹层
    if (m_pubPreviewAnim > 0.004f) PaintPubPreviewOverlay(cv);
    // 私有专栏预览弹层（视口坐标，不随滚动）
    else if (m_previewAnim > 0.004f) PaintPreviewOverlay(cv);
    // 编辑器弹层（视口坐标，不随滚动）
    else if (m_editAnim > 0.004f) PaintEditorOverlay(cv);

    // 滚动条
    if (MaxScroll() > 1.0f) {
        float trackTop = m_area.top + 8.0f;
        float trackH = (m_area.bottom - m_area.top) - 16.0f;
        float thumbH = (std::max)(40.0f, trackH * ((m_area.bottom - m_area.top) / m_contentHeight));
        float pp = Clamp01(s / MaxScroll());
        float ty = trackTop + (trackH - thumbH) * pp;
        float bx = m_area.right - 6.0f;
        cv.FillRect({ bx, trackTop, bx + 2.0f, trackTop + trackH }, WithAlpha(pal.ink300, 0.16f));
        cv.FillRect({ bx, ty, bx + 2.0f, ty + thumbH }, WithAlpha(pal.seal, 0.55f));
    }
}

void RoadmapView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    (void)contentW;
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION · 专栏 · 每账户私有可编辑", { x0, y, x0 + 420.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"专 栏", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void RoadmapView::PaintList(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    (void)contentW;
    float appear = Clamp01((m_t - 0.35f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    for (size_t i = 0; i < m_cols.size() && i < m_cardRects.size(); ++i) {
        const auto& c = m_cols[i];
        const auto& r = m_cardRects[i];
        bool hover = (m_hoverCard == (int)i);
        cv.PaperCard(r, 0.0f, shape::kEdge);
        // 左侧色条
        cv.FillRect({ r.left, r.top, r.left + 3.0f, r.bottom }, WithAlpha(pal.seal, 0.8f));
        // 标题
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 16.0f; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(c.title, { r.left + 18.0f, r.top + 16.0f, r.right - 276.0f, r.top + 42.0f }, ts, pal.ink900);
        // 元信息
        TextStyle ms; ms.role = FontRole::Mono; ms.size = 11.0f; ms.vAlign = VAlign::Top;
        std::wstring meta = (c.updatedAt > 0 ? (L"更新于 " + FormatTs(c.updatedAt)) : L"")
                          + (c.author.empty() ? L"" : (L"  ·  " + c.author));
        cv.Text(meta, { r.left + 18.0f, r.top + 44.0f, r.right - 18.0f, r.top + 60.0f }, ms, pal.ink500);
        // 正文预览（裁剪，长文不溢出）
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 12.0f; bs.vAlign = VAlign::Top;
        cv.Text(c.body, { r.left + 18.0f, r.top + 62.0f, r.right - 18.0f, r.bottom - 14.0f }, bs, pal.ink700);

        // 收藏态角标：不悬停时也要看得见「这条已收藏」
        bool faved = IsColFav(c.id);
        if (faved && !hover) {
            TextStyle fs; fs.role = FontRole::Sans; fs.size = 13.0f;
            fs.hAlign = HAlign::Right; fs.vAlign = VAlign::Middle;
            cv.Text(L"★", { r.right - 44.0f, r.top + 14.0f, r.right - 16.0f, r.top + 42.0f }, fs, pal.brass);
        }

        // 悬浮操作按钮
        if (hover) {
            auto Pill = [&](const D2D1_RECT_F& b, const wchar_t* label, bool danger, bool quiet) {
                D2D1_COLOR_F fill = danger ? WithAlpha(pal.vermilion, 0.14f)
                                           : (quiet ? WithAlpha(pal.rule, 0.16f) : WithAlpha(pal.seal, 0.14f));
                D2D1_COLOR_F col  = danger ? pal.vermilion : (quiet ? pal.ink700 : pal.seal);
                cv.FillRoundRect(b, 6.0f, fill);
                cv.StrokeRoundRect(b, 6.0f, col, shape::kHair);
                TextStyle ps; ps.role = FontRole::Sans; ps.size = 11.5f; ps.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                ps.hAlign = HAlign::Center; ps.vAlign = VAlign::Middle;
                cv.Text(label, b, ps, col);
            };
            if (i < m_favBtns.size()) {
                const auto& b = m_favBtns[i];
                D2D1_COLOR_F col = faved ? pal.brass : pal.ink700;
                cv.FillRoundRect(b, 6.0f, WithAlpha(pal.brass, faved ? 0.18f : 0.08f));
                cv.StrokeRoundRect(b, 6.0f, col, shape::kHair);
                TextStyle ps; ps.role = FontRole::Sans; ps.size = 11.5f;
                ps.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                ps.hAlign = HAlign::Center; ps.vAlign = VAlign::Middle;
                cv.Text(faved ? L"★ 已收藏" : L"☆ 收藏", b, ps, col);
            }
            if (i < m_viewBtns.size()) Pill(m_viewBtns[i], L"查看", false, true);
            if (i < m_editBtns.size()) Pill(m_editBtns[i], L"编辑", false, false);
            if (i < m_delBtns.size())  Pill(m_delBtns[i],  L"删除", true,  false);
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void RoadmapView::PaintEmpty(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.4f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    cv.PushOpacity(e);

    D2D1_RECT_F r = m_emptyRect;
    cv.PaperCard(r, 0.0f, shape::kEdge);
    cv.StrokeRoundRect(r, shape::kEdge, WithAlpha(pal.rule, 0.7f), shape::kHair);
    cv.NumberTag({ r.right - 64.0f, r.top + 12.0f, r.right - 14.0f, r.top + 30.0f },
                 L"空白", pal.sealWash, pal.seal, 10.0f);
    TextStyle t1; t1.role = FontRole::Sans; t1.size = 15.0f; t1.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(L"你还没有专栏。", { r.left + 22.0f, r.top + 40.0f, r.right - 22.0f, r.top + 70.0f }, t1, pal.ink900);
    TextStyle t2; t2.role = FontRole::Sans; t2.size = 12.5f; t2.vAlign = VAlign::Top;
    cv.Text(L"点右上角「+ 新建专栏」写下第一篇吧 —— 这是你自己的档案，内容只存于本账户，不和别人共享。",
            { r.left + 22.0f, r.top + 74.0f, r.right - 22.0f, r.bottom - 22.0f }, t2, pal.ink500);

    cv.PopOpacity();
}

void RoadmapView::PaintEditorOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = m_editAnim;
    ComputeEditorRects();
    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.6f * a));
    cv.PushOpacity(a);

    cv.PaperCard(m_edCard, 0.4f, shape::kEdge);
    cv.DoubleFrame(m_edCard, WithAlpha(pal.seal, 0.8f));

    TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 20.0f;
    ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
    cv.Text(m_editId.empty() ? L"新建专栏" : L"编辑专栏",
            { m_edCard.left + 26.0f, m_edCard.top + 18.0f,
              m_edCard.right - 26.0f, m_edCard.top + 50.0f }, ttl, pal.ink900);

    TextStyle hs; hs.role = FontRole::Mono; hs.size = 10.5f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"标题", { m_edTitleBox.left, m_edTitleBox.top - 20.0f,
                       m_edTitleBox.right, m_edTitleBox.top - 4.0f }, hs, pal.seal);
    // 标题：v2 统一输入框（编辑中文字/光标/选区/IME 由 D3D 绘制），保留编辑器卡边框
    {
        TextStyle ts; ts.role = FontRole::Serif; ts.size = 17.0f;
        ts.weight = DWRITE_FONT_WEIGHT_BOLD; ts.vAlign = VAlign::Middle;
        if (m_titleActive) {
            m_edTitle.Paint(cv, m_edTitleBox, ts, pal.ink900, L"未命名专栏", pal.ink300, 4.0f, 0.0f);
        } else {
            lj::PaintFieldEdit(cv, m_edTitleBox, ts,
                              m_edTitleBuf.empty() ? std::wstring(L"未命名专栏") : m_edTitleBuf,
                              m_edTitleBuf.empty() ? pal.ink300 : pal.ink900, -1, 4.0f);
        }
    }
    cv.Text(L"正文", { m_edBodyBox.left, m_edBodyBox.top - 20.0f,
                       m_edBodyBox.right, m_edBodyBox.top - 4.0f }, hs, pal.seal);

    // ---- 格式工具栏（D2D 绘制；点击经 Update 应用到 RichEdit 选区/插入点）----
    // 当前选区/插入点的字号与效果，用于高亮当前生效的按钮
    LONG curSize = 240;
    bool curBold = false;
    if (m_edBody) {
        CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
        SendMessageW(m_edBody, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
        if (cf.yHeight > 0) curSize = cf.yHeight;
        curBold = (cf.dwEffects & CFE_BOLD) != 0;
    }
    bool canUndo  = m_edBody && SendMessageW(m_edBody, EM_CANUNDO, 0, 0);
    bool canRedo  = m_edBody && SendMessageW(m_edBody, EM_CANREDO, 0, 0);
    WORD curAlign = EdGetAlign();
    WORD curNum   = EdGetNumbering();

    auto FmtBtn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool active, bool emph, bool enabled = true) {
        D2D1_COLOR_F fill = active ? pal.seal : pal.paperLo;
        D2D1_COLOR_F col  = active ? pal.paperHi : (enabled ? pal.ink700 : pal.ink300);
        if (!enabled) fill = WithAlpha(pal.rule, 0.12f);
        cv.FillRoundRect(r, 5.0f, fill);
        cv.StrokeRoundRect(r, 5.0f, active ? pal.seal : pal.rule, shape::kHair);
        TextStyle fs; fs.role = FontRole::Sans; fs.size = 12.5f;
        fs.weight = emph ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        fs.hAlign = HAlign::Center; fs.vAlign = VAlign::Middle;
        cv.Text(label, r, fs, col);
    };

    // 第一行
    FmtBtn(m_edUndo,  L"撤消", false, false, canUndo);
    FmtBtn(m_edRedo,  L"重做", false, false, canRedo);
    FmtBtn(m_edBrush, L"刷子", m_fmtBrush, false);
    FmtBtn(m_edClear, L"清除", false, false);
    FmtBtn(m_edH1, L"H1", curBold && curSize >= 400, true);
    FmtBtn(m_edH2, L"H2", curBold && curSize >= 340 && curSize < 400, true);
    FmtBtn(m_edH3, L"H3", curBold && curSize >= 280 && curSize < 340, true);
    FmtBtn(m_edBold,      L"B", EdHasEffect(CFE_BOLD),      true);
    FmtBtn(m_edItalic,    L"I", EdHasEffect(CFE_ITALIC),    false);
    FmtBtn(m_edUnderline, L"U", EdHasEffect(CFE_UNDERLINE), false);
    FmtBtn(m_edStrike,    L"S", EdHasEffect(CFE_STRIKEOUT), false);
    FmtBtn(m_edSizeS, L"小", curSize <= 200, false);
    FmtBtn(m_edSizeM, L"中", curSize > 200 && curSize < 290, false);
    FmtBtn(m_edSizeL, L"大", curSize >= 290, false);

    // 第二行
    FmtBtn(m_edColor,  L"字色", m_edColorMode == 1, false);
    FmtBtn(m_edBg,     L"底色", m_edColorMode == 2, false);
    FmtBtn(m_edAlignL, L"左", curAlign == PFA_LEFT,   false);
    FmtBtn(m_edAlignC, L"中", curAlign == PFA_CENTER, false);
    FmtBtn(m_edAlignR, L"右", curAlign == PFA_RIGHT,  false);
    FmtBtn(m_edOList, L"1.", curNum == PFN_NUMBERED, false);
    FmtBtn(m_edUList, L"•",  curNum == PFN_BULLET,   false);
    FmtBtn(m_edImage, L"图片", false, false);
    FmtBtn(m_edLink,  L"链接", false, false);
    FmtBtn(m_edHr,    L"分割", false, false);

    // ---- 颜色浮层（字体色 / 背景色）----
    if (m_edColorMode != 0 && !m_edSwatches.empty()) {
        cv.FillRoundRect(m_edSwatchPanel, 6.0f, pal.paperHi);
        cv.StrokeRoundRect(m_edSwatchPanel, 6.0f, pal.rule, shape::kHair);
        for (size_t i = 0; i < m_edSwatches.size(); ++i) {
            const auto& sr = m_edSwatches[i];
            COLORREF c = kSwatchCols[i];
            D2D1_COLOR_F fc{};
            fc.r = (float)GetRValue(c) / 255.0f;
            fc.g = (float)GetGValue(c) / 255.0f;
            fc.b = (float)GetBValue(c) / 255.0f;
            fc.a = 1.0f;
            cv.FillRoundRect(sr, 3.0f, fc);
            cv.StrokeRoundRect(sr, 3.0f, pal.rule, shape::kHair);
        }
    }

    // EDIT 由 Win32 绘制，这里只描边框给纸面感
    cv.StrokeRoundRect({ m_edTitleBox.left - 3.0f, m_edTitleBox.top - 3.0f,
                         m_edTitleBox.right + 3.0f, m_edTitleBox.bottom + 3.0f },
                       4.0f, WithAlpha(pal.rule, 0.9f), shape::kHair);
    cv.StrokeRoundRect({ m_edBodyBox.left - 3.0f, m_edBodyBox.top - 3.0f,
                         m_edBodyBox.right + 3.0f, m_edBodyBox.bottom + 3.0f },
                       4.0f, WithAlpha(pal.rule, 0.9f), shape::kHair);

    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary, bool danger) {
        cv.FillRoundRect(r, 6.0f, danger ? pal.vermilion : (primary ? pal.seal : pal.paperLo));
        cv.StrokeRoundRect(r, 6.0f, danger ? pal.vermilion : (primary ? pal.seal : pal.rule), shape::kHair);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f;
        bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(label, r, bs, danger ? pal.paperHi : (primary ? pal.paperHi : pal.ink700));
    };
    Btn(m_edCancel, L"取消", false, false);
    Btn(m_edSave,   L"保存",  true,  false);
    Btn(m_edPublish,L"发布",  false, false);   // 保存并发布到公共区
    if (!m_editId.empty()) Btn(m_edDelete, L"删除", false, true);

    cv.PopOpacity();
}

// ============================================================
//  编辑器（Win32 EDIT 承载中文 IME）
// ============================================================
void RoadmapView::ComputeEditorRects()
{
    const float areaW = m_area.right - m_area.left;
    const float areaH = m_area.bottom - m_area.top;
    const float w = (std::min)(700.0f, areaW - 80.0f);
    const float h = (std::min)(560.0f, areaH - 60.0f);
    const float cx = (m_area.left + m_area.right) * 0.5f;
    const float x = cx - w / 2.0f;
    float yy = (areaH - h) * 0.5f + m_area.top;
    if (yy < m_area.top + 12.0f) yy = m_area.top + 12.0f;
    m_edCard    = { x, yy, x + w, yy + h };
    m_edTitleBox = { x + 24.0f, yy + 78.0f,  x + w - 24.0f, yy + 112.0f };

    const float bx0 = x + 24.0f;
    const float row1Top = yy + 122.0f, row2Top = yy + 158.0f, bh = 30.0f, gap = 8.0f;

    // ---- 第一行 ----
    float x1 = bx0;
    auto put1 = [&](D2D1_RECT_F& r, float bw) { r = { x1, row1Top, x1 + bw, row1Top + bh }; x1 += bw + gap; };
    put1(m_edUndo, 38);  put1(m_edRedo, 38);
    put1(m_edBrush, 46); put1(m_edClear, 46);
    put1(m_edH1, 38);    put1(m_edH2, 38);    put1(m_edH3, 38);
    put1(m_edBold, 32);  put1(m_edItalic, 32); put1(m_edUnderline, 32); put1(m_edStrike, 32);
    put1(m_edSizeS, 40); put1(m_edSizeM, 40); put1(m_edSizeL, 40);

    // ---- 第二行 ----
    float x2 = bx0;
    auto put2 = [&](D2D1_RECT_F& r, float bw) { r = { x2, row2Top, x2 + bw, row2Top + bh }; x2 += bw + gap; };
    put2(m_edColor, 48);   put2(m_edBg, 48);
    put2(m_edAlignL, 34);  put2(m_edAlignC, 34);  put2(m_edAlignR, 34);
    put2(m_edOList, 42);   put2(m_edUList, 42);
    put2(m_edImage, 42);   put2(m_edLink, 42);     put2(m_edHr, 42);

    m_edBodyBox  = { x + 24.0f, yy + 196.0f, x + w - 24.0f, yy + h - 72.0f };
    const float bw = 110.0f, bbh = 36.0f;
    m_edCancel = { x + w - 24.0f - bw, yy + h - 52.0f, x + w - 24.0f, yy + h - 16.0f };
    m_edSave   = { m_edCancel.left - bw - 12.0f, m_edCancel.top, m_edCancel.left - 12.0f, m_edCancel.bottom };
    m_edPublish= { m_edSave.left - bw - 12.0f, m_edCancel.top, m_edSave.left - 12.0f, m_edCancel.bottom };
    m_edDelete = { m_edPublish.left - bw - 12.0f, m_edCancel.top, m_edPublish.left - 12.0f, m_edCancel.bottom };

    // ---- 颜色浮层（开启时放在卡片右侧，避免被正文 RichEdit 子窗口遮挡/吞点击）----
    m_edSwatches.clear();
    if (m_edColorMode != 0) {
        const float sw = 22.0f, sgap = 6.0f;
        const float pw = (float)kSwatchN * sw + (float)(kSwatchN - 1) * sgap + 16.0f;
        const float ph = sw + 16.0f;
        float px = m_edCard.right + 10.0f;
        if (px + pw > m_area.right) px = m_edCard.left - 10.0f - pw;
        if (px < m_area.left)       px = m_area.left + 8.0f;
        float py = (m_edColorMode == 1 ? m_edColor.bottom : m_edBg.bottom) - bh;
        for (int i = 0; i < kSwatchN; ++i) {
            m_edSwatches.push_back({ px + 8.0f + i * (sw + sgap), py + 8.0f,
                                     px + 8.0f + i * (sw + sgap) + sw, py + 8.0f + sw });
        }
        m_edSwatchPanel = { px, py, px + pw, py + ph };
    } else {
        m_edSwatchPanel = { 0, 0, 0, 0 };
    }
}

void RoadmapView::EnsureEditors()
{
    HWND parent = AppHwnd();
    if (!parent) return;
    if (!m_edFont) {
        m_edFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");
    }
    // RichEdit 依赖系统 Msftedit.dll（非第三方），加载一次常驻即可
    static HMODULE sRich = LoadLibraryW(L"Msftedit.dll");
    (void)sRich;

    // 标题已改用 v2 统一输入框（1×1 透明代理 + D3D 自绘，无白块），无需创建 EDIT。
    // 正文 RichEdit 保留原生交互（可见框内点击定位/框选本来就正常）。
    if (!m_edBody) {
        DWORD style = WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE
                    | ES_AUTOVSCROLL | ES_LEFT | ES_WANTRETURN;
        // 使用 RichEdit 4.1+ 类名（RICHEDIT50W 宏在本 SDK 未声明，直接用字面量）
        m_edBody = CreateWindowExW(0, L"RICHEDIT50W", L"",
                            style, 0, 0, 10, 10, parent, nullptr,
                            (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
        if (m_edBody && m_edFont) SendMessageW(m_edBody, WM_SETFONT, (WPARAM)m_edFont, TRUE);
        if (m_edBody) {
            // 默认字符格式：微软雅黑 12pt（240 twips），让新建正文有合适字号
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
            cf.yHeight = 240;
            cf.bCharSet = DEFAULT_CHARSET;
            cf.crTextColor = RgbOf(lj::AppPalette().ink900); // 深色主题下正文也要可读
            wcscpy_s(cf.szFaceName, L"Microsoft YaHei UI");
            SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
            // 背景：与编辑器卡面色一致（lift=0.4），输入框不再是一块白/暗矩形
            SendMessageW(m_edBody, EM_SETBKGNDCOLOR, 0, (LPARAM)RgbOf(lj::CardFace(0.4f)));
            // 自动识别 URL 为链接（插入「链接」时键入网址即转为可点链接）
            SendMessageW(m_edBody, EM_AUTOURLDETECT, 1, 0);
            ShowWindow(m_edBody, SW_HIDE);
        }
    }
}

// 标题编辑（v2 统一输入框）：失焦/回车时把缓冲落回 m_edTitleBuf
void RoadmapView::CommitTitleEdit()
{
    if (!m_titleActive) return;
    std::wstring t;
    m_edTitle.End(true, t);
    m_edTitleBuf = t;
    m_titleActive = false;
}

void RoadmapView::DestroyEditors()
{
    m_edTitle.Cancel();
    m_titleActive = false;
    if (m_edBody)  { DestroyWindow(m_edBody);  m_edBody  = nullptr; }
    if (m_edFont)  { DeleteObject(m_edFont);   m_edFont  = nullptr; }
}

// 当前选区 / 插入点是否含某字符效果（如 CFE_BOLD / CFE_ITALIC）
bool RoadmapView::EdHasEffect(DWORD eff) const
{
    if (!m_edBody) return false;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    SendMessageW(m_edBody, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    return (cf.dwEffects & eff) != 0;
}

// 对选区 / 插入点设置字符格式（mask 决定改哪些属性；yHeight 仅在 CFM_SIZE 时有效，单位 twips）
void RoadmapView::EdApplyFormat(DWORD mask, DWORD effects, LONG yHeight)
{
    if (!m_edBody) return;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    cf.dwMask = mask;
    cf.dwEffects = effects;
    if (mask & CFM_SIZE) cf.yHeight = yHeight;
    SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// 段落格式（对齐 / 列表）
void RoadmapView::EdApplyPara(DWORD mask, WORD alignment, WORD numbering)
{
    if (!m_edBody) return;
    PARAFORMAT2 pf{}; pf.cbSize = sizeof(pf);
    pf.dwMask = mask;
    if (mask & PFM_ALIGNMENT) pf.wAlignment = alignment;
    if (mask & PFM_NUMBERING) pf.wNumbering = numbering;
    SendMessageW(m_edBody, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void RoadmapView::EdUndo()  { if (m_edBody) SendMessageW(m_edBody, EM_UNDO, 0, 0); }
void RoadmapView::EdRedo()  { if (m_edBody) SendMessageW(m_edBody, EM_REDO, 0, 0); }

void RoadmapView::EdClearFormat()
{
    if (!m_edBody) return;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    cf.dwMask   = CFM_ALL | CFM_BACKCOLOR;   // CFM_ALL 不含背景色，单独补上
    cf.dwEffects = 0;            // 清掉所有效果位（粗/斜/下划线/删除线/超级链接等）
    cf.yHeight  = 240;           // 回到 12pt
    cf.bCharSet = DEFAULT_CHARSET;
    wcscpy_s(cf.szFaceName, L"Microsoft YaHei UI");
    cf.crTextColor = RGB(0, 0, 0);
    cf.crBackColor = RGB(255, 255, 255);
    SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    // 段落：回到左对齐、无列表
    PARAFORMAT2 pf{}; pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT | PFM_NUMBERING;
    pf.wAlignment = PFA_LEFT; pf.wNumbering = 0;
    SendMessageW(m_edBody, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void RoadmapView::EdApplyHeading(int level)
{
    if (!m_edBody) return;
    LONG yh = level == 1 ? 440 : (level == 2 ? 360 : 300);   // 22 / 18 / 15 pt
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    cf.yHeight = yh;
    SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void RoadmapView::EdSetColor(COLORREF c, bool bg)
{
    if (!m_edBody) return;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    if (bg) { cf.dwMask = CFM_BACKCOLOR; cf.crBackColor = c; }
    else    { cf.dwMask = CFM_COLOR;     cf.crTextColor = c; }
    SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void RoadmapView::EdSetAlignment(WORD align)
{
    EdApplyPara(PFM_ALIGNMENT, align, 0);
}

void RoadmapView::EdToggleList(WORD kind)
{
    if (!m_edBody) return;
    PARAFORMAT2 pf{}; pf.cbSize = sizeof(pf);
    SendMessageW(m_edBody, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    WORD cur = (pf.dwMask & PFM_NUMBERING) ? pf.wNumbering : 0;
    pf.dwMask = PFM_NUMBERING;
    pf.wNumbering = (cur == kind) ? 0 : kind;     // 再点一次同类型 → 取消
    if (kind == PFN_BULLET) pf.dxOffset = 200;    // 项目符号缩进
    SendMessageW(m_edBody, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

void RoadmapView::EdFormatBrush()
{
    if (!m_edBody) return;
    if (!m_fmtBrush) {
        // 取当前选区 / 插入点格式，进入「刷」模式
        m_fmtBrushCf.cbSize = sizeof(m_fmtBrushCf);
        SendMessageW(m_edBody, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&m_fmtBrushCf);
        m_fmtBrushPf.cbSize = sizeof(m_fmtBrushPf);
        SendMessageW(m_edBody, EM_GETPARAFORMAT, 0, (LPARAM)&m_fmtBrushPf);
        m_fmtBrush = true;
    } else {
        // 把取到的格式刷到当前选区
        SendMessageW(m_edBody, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&m_fmtBrushCf);
        if (m_fmtBrushPf.dwMask & (PFM_ALIGNMENT | PFM_NUMBERING))
            SendMessageW(m_edBody, EM_SETPARAFORMAT, 0, (LPARAM)&m_fmtBrushPf);
        m_fmtBrush = false;
    }
}

WORD RoadmapView::EdGetAlign() const
{
    if (!m_edBody) return PFA_LEFT;
    PARAFORMAT2 pf{}; pf.cbSize = sizeof(pf);
    SendMessageW(m_edBody, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    return (pf.dwMask & PFM_ALIGNMENT) ? pf.wAlignment : PFA_LEFT;
}

WORD RoadmapView::EdGetNumbering() const
{
    if (!m_edBody) return 0;
    PARAFORMAT2 pf{}; pf.cbSize = sizeof(pf);
    SendMessageW(m_edBody, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
    return (pf.dwMask & PFM_NUMBERING) ? pf.wNumbering : 0;
}

// 在插入点 / 选区处流式插入一段 RTF（SF_SELECTION：零长度选区即插入，有选区则替换）
bool RoadmapView::EdInsertRtf(const std::string& rtf)
{
    if (!m_edBody) return false;
    SetFocus(m_edBody);
    // 包一层最小 RTF 文档，确保 EM_STREAMIN(SF_SELECTION) 能正确解析并合并
    std::string full = "{\\rtf1\\ansi\\ansicpg936 " + rtf + "}";
    EDITSTREAM es{};
    es.dwCookie  = (DWORD_PTR)&full;
    es.pfnCallback = RtfInCallback;       // 复用流式「入」回调（App→控件）
    LRESULT r = SendMessageW(m_edBody, EM_STREAMIN, SF_RTF | SF_SELECTION, (LPARAM)&es);
    return r == 0;
}

void RoadmapView::EdInsertLink()
{
    if (!m_edBody) return;
    SetFocus(m_edBody);
    // 正文框已开 EM_AUTOURLDETECT：插入 URL 文本即被自动识别为可点链接
    const wchar_t* url = L" https://www.example.com ";
    SendMessageW(m_edBody, EM_REPLACESEL, TRUE, (LPARAM)url);
}

void RoadmapView::EdInsertDivider()
{
    // 段落下边框 = 一条水平分割线
    EdInsertRtf("{\\pard\\brdrb\\brdrs\\brdrw12\\brsp20 \\par}");
}

void RoadmapView::EdInsertImage()
{
    if (!m_edBody) return;
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = AppHwnd();
    ofn.lpstrFilter = L"图片 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0\0";
    wchar_t file[MAX_PATH] = { 0 };
    ofn.lpstrFile  = file;
    ofn.nMaxFile   = MAX_PATH;
    ofn.Flags      = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::ifstream f(file, std::ios::binary);
    if (!f) return;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (buf.size() < 8) return;

    // 解析 PNG 尺寸（IHDR：偏移 16 起 8 字节为宽/高，大端）
    unsigned iw = 160, ih = 120;
    const unsigned char* p = buf.data();
    bool isPng = (buf.size() >= 24 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G');
    if (isPng) {
        iw = (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
        ih = (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
    }
    bool isJpg = (buf.size() >= 2 && p[0] == 0xFF && p[1] == 0xD8);

    std::string hex; hex.reserve(buf.size() * 2 + 16);
    static const char* d = "0123456789abcdef";
    for (unsigned char c : buf) { hex.push_back(d[c >> 4]); hex.push_back(d[c & 0xF]); }

    std::string blip = isJpg ? "\\jpegblip" : "\\pngblip";
    std::string rtf = "{\\pict" + blip
                    + "\\picw" + std::to_string(iw)
                    + "\\pich" + std::to_string(ih)
                    + " " + hex + "}";
    EdInsertRtf(rtf);
}

void RoadmapView::OpenEditor(const std::wstring& id)
{
    m_editId = id;
    m_editing = true;
    m_editAnim = 0.0f;
    ComputeEditorRects();
    EnsureEditors();
    // 每次打开都按当前主题设 RichEdit 背景，确保与编辑器卡面色一致（深色下不再白块）
    if (m_edBody) SendMessageW(m_edBody, EM_SETBKGNDCOLOR, 0, (LPARAM)RgbOf(lj::CardFace(0.4f)));
    std::wstring t, b; std::string rtf;
    if (!id.empty()) {
        for (auto& c : m_cols)
            if (c.id == id) { t = c.title; b = c.body; rtf = c.bodyRtf; break; }
    }
    // 标题：v2 统一输入框持有缓冲，失焦/回车时落回 m_edTitleBuf；
    // 不在此处 Begin（焦点先给正文），点击标题框时再进入编辑。
    m_edTitleBuf = t;
    m_titleActive = false;
    // 正文：有 RTF 则流式载入富文本，否则按纯文本设置
    if (m_edBody) {
        if (!rtf.empty()) {
            EDITSTREAM es{}; es.dwCookie = (DWORD_PTR)&rtf; es.pfnCallback = RtfInCallback;
            SendMessageW(m_edBody, EM_STREAMIN, SF_RTF, (LPARAM)&es);
        } else {
            SetWindowTextW(m_edBody, b.c_str());
        }
    }
    const float s = (float)AppDpi() / 96.0f;
    if (m_edBody) {
        SetWindowPos(m_edBody, nullptr, (int)(m_edBodyBox.left * s), (int)(m_edBodyBox.top * s),
                     (int)((m_edBodyBox.right - m_edBodyBox.left) * s),
                     (int)((m_edBodyBox.bottom - m_edBodyBox.top) * s), SWP_NOZORDER);
        ShowWindow(m_edBody, SW_SHOW);
        SetFocus(m_edBody);
    }
}

void RoadmapView::CloseEditor(bool save)
{
    if (save) {
        CommitTitleEdit();                        // 标题缓冲落盘
        auto grab = [](HWND h) -> std::wstring {
            if (!h) return L"";
            int n = GetWindowTextLengthW(h);
            std::wstring t; t.resize((size_t)n + 1);
            GetWindowTextW(h, &t[0], n + 1);
            t.resize((size_t)n);
            return t;
        };
        std::wstring title = m_edTitleBuf;
        std::wstring body  = grab(m_edBody);
        std::string  rtf   = EdGetRtf(m_edBody);   // 富文本（加粗/字号等格式）
        if (title.empty()) title = L"未命名专栏";

        auto& store = CheckinStore::Instance();
        auto cols = store.LoadColumns();
        long long now = (long long)time(nullptr);
        if (m_editId.empty()) {
            Column c;
            c.id = GenId();
            c.title = title;
            c.body = body;
            c.bodyRtf = rtf;
            c.author = AccountStore::Instance().CurrentName();
            c.createdAt = now;
            c.updatedAt = now;
            cols.insert(cols.begin(), c);   // 新建置顶
        } else {
            for (auto& c : cols)
                if (c.id == m_editId) { c.title = title; c.body = body; c.bodyRtf = rtf; c.updatedAt = now; break; }
        }
        store.SaveColumns(cols);
    }
    // 标题编辑会话收尾（若还在编辑中）
    CommitTitleEdit();
    m_edTitleBuf.clear();
    if (m_edBody)  ShowWindow(m_edBody,  SW_HIDE);
    if (AppHwnd()) SetFocus(AppHwnd());
    m_editing = false;
    m_editId.clear();
    m_edColorMode = 0;
    m_fmtBrush = false;
    LoadColumns();
    RelayoutList();
}

// 发布到公共区：保存本地 + POST /columns 上云（结果经 m_pubResult 原子码由 Update 消费提示）
void RoadmapView::DoPublish()
{
    auto grab = [](HWND h) -> std::wstring {
        if (!h) return L"";
        int n = GetWindowTextLengthW(h);
        std::wstring t; t.resize((size_t)n + 1);
        GetWindowTextW(h, &t[0], n + 1);
        t.resize((size_t)n);
        return t;
    };
    CommitTitleEdit();
    std::wstring title = m_edTitleBuf;
    std::wstring body  = grab(m_edBody);
    // 去首尾空白后再判空
    size_t a = title.find_first_not_of(L" \t");
    size_t b = title.find_last_not_of(L" \t");
    title = (a == std::wstring::npos) ? L"" : title.substr(a, b - a + 1);
    if (title.empty()) {
        m_pubHint = L"先填写标题再发布";
        m_pubHintUntil = (long long)time(nullptr) + 5;
        return;
    }
    // 先保存本地（复用 CloseEditor(true) 的保存+关闭），再异步上云
    CloseEditor(true);
    if (!Cloud::Instance().LoggedIn()) {
        m_pubHint = L"未登录云端：已保存到本地（登录后点「发布」上传公共区）";
        m_pubHintUntil = (long long)time(nullptr) + 5;
        return;
    }
    Cloud::RunAsync([this, title, body]() {
        std::wstring e;
        CloudResult cr = Cloud::Instance().CreateColumn(title, body, L"general", e);
        m_pubResult.store(cr == CloudResult::Ok ? 1 : 2);
        if (cr == CloudResult::Ok) FetchPublicColumns();   // 后台重拉公共列表（内部 RunAsync，安全）
    });
}

void RoadmapView::DeleteColumn(const std::wstring& id)
{
    auto& store = CheckinStore::Instance();
    auto cols = store.LoadColumns();
    cols.erase(std::remove_if(cols.begin(), cols.end(),
                [&](const Column& c) { return c.id == id; }), cols.end());
    store.SaveColumns(cols);
    LoadColumns();
    RelayoutList();
}

std::wstring RoadmapView::FormatTs(long long ts) const
{
    if (ts <= 0) return L"";
    time_t t = (time_t)ts;
    struct tm tm {};
    localtime_s(&tm, &t);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::wstring RoadmapView::GenId() const
{
    static long long s_seq = 0;
    return L"col_" + std::to_wstring((long long)time(nullptr)) + L"_"
           + std::to_wstring(++s_seq) + L"_" + std::to_wstring((long long)this);
}

void RoadmapView::DebugForceOpen()
{
    if (!m_cols.empty()) OpenEditor(m_cols.front().id);
}

void RoadmapView::DebugForcePreview()
{
    if (!m_cols.empty()) OpenPreview(m_cols.front().id);
}

// ============================================================
//  预览弹层（只读，点卡片进入）
// ============================================================
void RoadmapView::OpenPreview(const std::wstring& id)
{
    m_previewId = id;
    m_previewing = true;
    m_previewAnim = 0.0f;
    m_previewScroll = 0.0f;
    ComputePreviewRects();
    // 记一笔浏览足迹，供个人界面「我的 · 历史」回看
    for (const auto& c : m_cols)
        if (c.id == id) { CheckinStore::Instance().PushHistory(L"column", c.id, c.title); break; }
}

void RoadmapView::ClosePreview()
{
    m_previewing = false;
    m_previewId.clear();
}

void RoadmapView::ComputePreviewRects()
{
    const float areaW = m_area.right - m_area.left;
    const float areaH = m_area.bottom - m_area.top;
    float w = areaW - 64.0f; if (w > 680.0f) w = 680.0f;
    float h = areaH - 60.0f; if (h > 580.0f) h = 580.0f;
    const float x = m_area.left + (areaW - w) * 0.5f;
    const float y = m_area.top + (areaH - h) * 0.5f;
    m_pvCard  = { x, y, x + w, y + h };
    m_pvBody  = { x + 30.0f, y + 116.0f, x + w - 30.0f, y + h - 78.0f };
    const float bw = 96.0f, bh = 36.0f;
    m_pvClose  = { x + w - 30.0f - bw,        y + h - 56.0f, x + w - 30.0f,        y + h - 20.0f };
    m_pvEdit   = { m_pvClose.left - bw - 12.0f, m_pvClose.top, m_pvClose.left - 12.0f, m_pvClose.bottom };
    m_pvDelete = { m_pvEdit.left - bw - 12.0f,  m_pvClose.top, m_pvEdit.left - 12.0f,  m_pvClose.bottom };
}

void RoadmapView::PaintPreviewOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = m_previewAnim;
    ComputePreviewRects();
    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.62f * a));
    cv.PushOpacity(a);

    cv.PaperCard(m_pvCard, 0.5f, shape::kEdge);
    cv.DoubleFrame(m_pvCard, WithAlpha(pal.seal, 0.85f));

    // 找到专栏
    const Column* c = nullptr;
    for (auto& it : m_cols) if (it.id == m_previewId) { c = &it; break; }
    std::wstring title = c ? c->title : L"";
    std::wstring body  = c ? c->body  : L"";
    std::wstring meta  = c ? ((c->updatedAt > 0 ? (L"更新于 " + FormatTs(c->updatedAt)) : L"")
                              + (c->author.empty() ? L"" : (L"  ·  " + c->author))) : L"";

    TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 24.0f;
    ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
    cv.Text(title, { m_pvCard.left + 30.0f, m_pvCard.top + 22.0f,
                     m_pvCard.right - 30.0f, m_pvCard.top + 60.0f }, ttl, pal.ink900);

    TextStyle ms; ms.role = FontRole::Mono; ms.size = 11.5f; ms.letterSpacing = 1.0f;
    cv.Text(meta, { m_pvCard.left + 30.0f, m_pvCard.top + 66.0f,
                    m_pvCard.right - 30.0f, m_pvCard.top + 86.0f }, ms, pal.ink500);

    // 正文（裁剪 + 滚轮滚动）
    TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.5f; bs.vAlign = VAlign::Top;
    bs.weight = DWRITE_FONT_WEIGHT_NORMAL;
    float bodyW = m_pvBody.right - m_pvBody.left;
    m_pvContentH = cv.MeasureHeight(body, bs, bodyW);   // 实测高度，供滚轮裁剪

    cv.PushClip(m_pvBody);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -m_previewScroll));
    cv.Text(body, { m_pvBody.left, m_pvBody.top, m_pvBody.right, m_pvBody.top + m_pvContentH + 40.0f },
            bs, pal.ink900);
    cv.PopTransform();
    cv.PopClip();

    // 正文底部若被裁剪：纸面渐隐提示 + 右侧可见滚动条（指示可滚动与位置）
    float wh = m_pvBody.bottom - m_pvBody.top;
    if (m_pvContentH > wh + 1.0f) {
        cv.FillRect({ m_pvBody.left, m_pvBody.bottom - 22.0f, m_pvBody.right, m_pvBody.bottom },
                    WithAlpha(pal.paperHi, 0.85f));
        float bx = m_pvBody.right + 9.0f;
        float trackTop = m_pvBody.top, trackBot = m_pvBody.bottom;
        cv.FillRoundRect({ bx - 2.0f, trackTop, bx + 2.0f, trackBot }, 2.0f, WithAlpha(pal.ink300, 0.18f));
        float th = (std::max)(28.0f, wh * wh / m_pvContentH);
        float ty = trackTop + (trackBot - trackTop - th) * (m_previewScroll / (m_pvContentH - wh));
        cv.FillRoundRect({ bx - 2.0f, ty, bx + 2.0f, ty + th }, 2.0f, WithAlpha(pal.seal, 0.6f));
    }

    // 按钮
    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary, bool danger) {
        cv.FillRoundRect(r, 6.0f, danger ? pal.vermilion : (primary ? pal.seal : pal.paperLo));
        cv.StrokeRoundRect(r, 6.0f, danger ? pal.vermilion : (primary ? pal.seal : pal.rule), shape::kHair);
        TextStyle bs2; bs2.role = FontRole::Sans; bs2.size = 13.0f; bs2.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs2.hAlign = HAlign::Center; bs2.vAlign = VAlign::Middle; bs2.letterSpacing = 1.0f;
        cv.Text(label, r, bs2, danger ? pal.paperHi : (primary ? pal.paperHi : pal.ink700));
    };
    Btn(m_pvClose,  L"关闭", false, false);
    Btn(m_pvEdit,   L"编辑", true,  false);
    if (!m_previewId.empty()) Btn(m_pvDelete, L"删除", false, true);

    cv.PopOpacity();
}

// ============================================================
//  §4 公共专栏详情弹层（列表 → 详情 → 点赞/收藏/评论）
// ============================================================
void RoadmapView::OpenPubPreview(const std::string& id)
{
    if (id.empty()) return;
    m_pubPreviewId = id;
    m_pubPreviewing = true;
    m_pubPreviewAnim = 0.0f;
    m_pubPreviewScroll = 0.0f;
    m_pubDetail = {};
    ComputePubPreviewRects();
    LoadPubDetail(id);
}

void RoadmapView::ClosePubPreview()
{
    if (m_pubCommentEdit) EndPubCommentEdit(false);
    m_pubPreviewing = false;
    m_pubPreviewId.clear();
}

void RoadmapView::ComputePubPreviewRects()
{
    const float areaW = m_area.right - m_area.left;
    const float areaH = m_area.bottom - m_area.top;
    float w = areaW - 64.0f; if (w > 720.0f) w = 720.0f;
    float h = areaH - 60.0f; if (h > 620.0f) h = 620.0f;
    const float x = m_area.left + (areaW - w) * 0.5f;
    const float y = m_area.top + (areaH - h) * 0.5f;
    m_pubPvCard = { x, y, x + w, y + h };
    m_pubPvBody = { x + 28.0f, y + 96.0f, x + w - 28.0f, y + h - 88.0f };
    m_pubPvClose = { x + w - 30.0f - 84.0f, y + h - 52.0f, x + w - 30.0f, y + h - 16.0f };
    // 点赞 / 收藏 按钮放在标题下方
    float bx = x + 28.0f;
    m_pubPvLike = { bx, y + 60.0f, bx + 70.0f, y + 88.0f };
    bx += 78.0f;
    m_pubPvFav  = { bx, y + 60.0f, bx + 70.0f, y + 88.0f };
    // 评论输入框在 body 底部
    float iy = m_pubPvBody.bottom - 44.0f;
    m_pubPvInput = { m_pubPvBody.left + 8.0f, iy, m_pubPvBody.right - 78.0f, iy + 36.0f };
    m_pubPvSend  = { m_pubPvBody.right - 66.0f, iy, m_pubPvBody.right - 8.0f, iy + 36.0f };
}

void RoadmapView::LoadPubDetail(const std::string& id)
{
    Cloud::RunAsync([this, id] {
        auto r = Cloud::Instance().GetColumn(id);
        std::lock_guard<std::mutex> lk(m_pubDetailMu);
        m_pubDetailResp = r;
        m_pubDetailDirty.store(true);
    });
}

void RoadmapView::OnPubDetail(const net::Response& r)
{
    if (!r.Ok()) { m_pubDetail.loaded = false; return; }
    lj::json::Parser pp(r.body.data(), r.body.size());
    lj::json::JVal root = pp.parse();
    const lj::json::JVal* post = lj::json::JGet(root, "post");
    if (!post || !post->IsObj()) return;

    PubDetail d;
    d.post.id = lj::json::JStr(*post, "id");
    d.post.title = net::FromUtf8(lj::json::JStr(*post, "title"));
    d.post.author = net::FromUtf8(lj::json::JStr(*post, "author"));
    const auto* ca = lj::json::JGet(*post, "createdAt");
    if (ca) d.post.createdAt = (long long)ca->num;
    const auto* ua = lj::json::JGet(*post, "updatedAt");
    if (ua) d.post.updatedAt = (long long)ua->num;
    const auto* lc = lj::json::JGet(*post, "likeCount");
    if (lc) d.post.likeCount = (int)lc->num;
    const auto* cc = lj::json::JGet(*post, "commentCount");
    if (cc) d.post.commentCount = (int)cc->num;
    const auto* fc = lj::json::JGet(*post, "favCount");
    if (fc) d.post.favCount = (int)fc->num;
    const auto* lk = lj::json::JGet(*post, "liked");
    if (lk) d.post.liked = lk->bval;
    const auto* fv = lj::json::JGet(*post, "faved");
    if (fv) d.post.faved = fv->bval;
    const auto* mn = lj::json::JGet(*post, "mine");
    if (mn) d.post.mine = mn->bval;
    d.body = net::FromUtf8(lj::json::JStr(*post, "body"));

    const lj::json::JVal* cmts = lj::json::JGet(*post, "comments");
    if (cmts && cmts->IsArr()) {
        for (const auto& c : cmts->arr) {
            NetComment nc;
            nc.user = net::FromUtf8(lj::json::JStr(c, "user"));
            nc.text = net::FromUtf8(lj::json::JStr(c, "text"));
            const auto* ct = lj::json::JGet(c, "ts");
            if (ct) nc.ts = (long long)ct->num;
            d.comments.push_back(std::move(nc));
        }
    }
    d.loaded = true;
    m_pubDetail = std::move(d);
    // 更新列表快照里的 like/fav 计数（详情里可能更新过）
    for (auto& p : m_pubPosts) {
        if (p.id == m_pubPreviewId) {
            p.likeCount = m_pubDetail.post.likeCount;
            p.favCount = m_pubDetail.post.favCount;
            p.commentCount = m_pubDetail.post.commentCount;
            p.liked = m_pubDetail.post.liked;
            p.faved = m_pubDetail.post.faved;
            break;
        }
    }
}

// ---- 评论输入（v2 统一输入框）----
void RoadmapView::BeginPubCommentEdit()
{
    if (m_pubCommentEdit) return;
    m_pubCommentEdit = true;
    m_pubComment.onEnter     = [this] { EndPubCommentEdit(true); };
    m_pubComment.onEsc       = [this] { m_pubCommentDraft.clear(); EndPubCommentEdit(false); };
    m_pubComment.onKillFocus = [this] { EndPubCommentEdit(false); };
    m_pubComment.Begin(m_pubCommentDraft, false, 12.5f);
}

void RoadmapView::EndPubCommentEdit(bool submit)
{
    if (!m_pubCommentEdit) return;
    std::wstring t;
    m_pubComment.End(true, t);
    if (submit && !t.empty()) {
        std::string pid = m_pubPreviewId;
        Cloud::RunAsync([this, pid, t] {
            Cloud::Instance().CommentColumn(pid, t);
            FetchPublicColumns();
            LoadPubDetail(pid);
        });
        m_pubCommentDraft.clear();
    } else {
        m_pubCommentDraft = t;
    }
    m_pubCommentEdit = false;
}

void RoadmapView::SubmitPubComment()
{
    EndPubCommentEdit(true);
}

void RoadmapView::PaintPubPreviewOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = m_pubPreviewAnim;
    // 弹层淡入淡出
    float target = m_pubPreviewing ? 1.0f : 0.0f;
    m_pubPreviewAnim += (target - m_pubPreviewAnim) * 0.25f;
    if (!m_pubPreviewing && m_pubPreviewAnim < 0.004f) m_pubPreviewAnim = 0.0f;
    ComputePubPreviewRects();
    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.62f * a));
    cv.PushOpacity(a);

    cv.PaperCard(m_pubPvCard, 0.5f, shape::kEdge);
    cv.DoubleFrame(m_pubPvCard, WithAlpha(pal.seal, 0.85f));

    const auto& d = m_pubDetail;
    TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 24.0f;
    ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
    cv.Text(d.loaded ? d.post.title : L"加载中…",
            { m_pubPvCard.left + 28.0f, m_pubPvCard.top + 22.0f,
              m_pubPvCard.right - 28.0f, m_pubPvCard.top + 60.0f }, ttl, pal.ink900);

    // 点赞 / 收藏 按钮
    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool active, bool primary) {
        cv.FillRoundRect(r, 6.0f, active ? WithAlpha(pal.seal, 0.16f) : WithAlpha(pal.rule, 0.2f));
        cv.StrokeRoundRect(r, 6.0f, active ? pal.seal : WithAlpha(pal.rule, 0.7f), shape::kHair);
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 12.0f; ls.hAlign = HAlign::Center; ls.vAlign = VAlign::Middle;
        cv.Text(label, r, ls, active ? pal.seal : pal.ink500);
    };
    wchar_t likeBuf[32], favBuf[32];
    swprintf_s(likeBuf, L"%s %d", d.post.liked ? L"♥" : L"♡", d.post.likeCount);
    swprintf_s(favBuf, L"%s %d", d.post.faved ? L"★" : L"☆", d.post.favCount);
    Btn(m_pubPvLike, likeBuf, d.post.liked, false);
    Btn(m_pubPvFav,  favBuf,  d.post.faved, false);

    TextStyle ms; ms.role = FontRole::Mono; ms.size = 11.0f; ms.letterSpacing = 1.0f;
    std::wstring meta = d.post.author + L"  ·  " + FormatTs(d.post.createdAt);
    cv.Text(meta, { m_pubPvCard.left + 28.0f, m_pubPvCard.top + 66.0f,
                    m_pubPvCard.left + 220.0f, m_pubPvCard.top + 84.0f }, ms, pal.ink500);

    // body + comments 统一绘制在 m_pubPvBody 内，底部留出评论输入框
    float inputH = 48.0f;
    D2D1_RECT_F bodyR = { m_pubPvBody.left, m_pubPvBody.top, m_pubPvBody.right, m_pubPvBody.bottom - inputH };

    // 拼合正文与评论的显示文本
    std::wstring full = d.loaded ? d.body : L"";
    if (d.loaded && !d.comments.empty()) {
        full += L"\n\n—— 评论 ——\n";
        for (const auto& c : d.comments) {
            full += c.user + L"：" + c.text + L"\n";
        }
    }

    TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f; bs.vAlign = VAlign::Top;
    bs.weight = DWRITE_FONT_WEIGHT_NORMAL;
    float bodyW = bodyR.right - bodyR.left;
    m_pubPvContentH = cv.MeasureHeight(full, bs, bodyW);

    cv.PushClip(bodyR);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -m_pubPreviewScroll));
    cv.Text(full, { bodyR.left, bodyR.top, bodyR.right, bodyR.top + m_pubPvContentH + 40.0f }, bs, pal.ink900);
    cv.PopTransform();
    cv.PopClip();

    // 滚动条
    float wh = bodyR.bottom - bodyR.top;
    if (m_pubPvContentH > wh + 1.0f) {
        cv.FillRect({ bodyR.left, bodyR.bottom - 22.0f, bodyR.right, bodyR.bottom },
                    WithAlpha(pal.paperHi, 0.85f));
        float bx = bodyR.right + 9.0f;
        cv.FillRoundRect({ bx - 2.0f, bodyR.top, bx + 2.0f, bodyR.bottom }, 2.0f, WithAlpha(pal.ink300, 0.18f));
        float th = (std::max)(28.0f, wh * wh / m_pubPvContentH);
        float ty = bodyR.top + (bodyR.bottom - bodyR.top - th) * (m_pubPreviewScroll / (m_pubPvContentH - wh));
        cv.FillRoundRect({ bx - 2.0f, ty, bx + 2.0f, ty + th }, 2.0f, WithAlpha(pal.seal, 0.6f));
    }

    // 评论输入框 + 发送按钮
    cv.FillRoundRect(m_pubPvInput, shape::kEdgeSoft, WithAlpha(pal.paperLo, 0.7f));
    cv.StrokeRoundRect(m_pubPvInput, shape::kEdgeSoft, pal.rule, shape::kHair);
    TextStyle pt; pt.role = FontRole::Sans; pt.size = 12.5f; pt.vAlign = VAlign::Middle;
    {
        D2D1_RECT_F tbox{ m_pubPvInput.left + 12.0f, m_pubPvInput.top,
                          m_pubPvInput.right - 12.0f, m_pubPvInput.bottom };
        if (m_pubCommentEdit) {
            // v2 统一输入框：文字/光标/选区/IME 组合串全由 D3D 绘制
            m_pubComment.Paint(cv, tbox, pt, pal.ink900, L"写评论…", pal.ink300, 0.0f, 0.0f);
        } else if (m_pubCommentDraft.empty()) {
            cv.Text(L"写评论…", tbox, pt, pal.ink300);
        } else {
            cv.Text(m_pubCommentDraft, tbox, pt, pal.ink900);
        }
    }
    Btn(m_pubPvSend, L"发送", false, true);
    Btn(m_pubPvClose, L"关闭", false, false);

    cv.PopOpacity();
}

// ============================================================
//  §4 公共专栏：云端拉取 / 实时事件 / 快照
// ============================================================
void RoadmapView::FetchPublicColumns()
{
    Cloud::RunAsync([this] {
        auto r = Cloud::Instance().GetColumns();
        if (!r.Ok()) return;
        // 解析 { sections: [...], posts: [...] }
        lj::json::Parser pp(r.body.data(), r.body.size());
        lj::json::JVal root = pp.parse();
        if (!root.IsObj()) return;

        std::vector<NetPost>    posts;
        std::vector<NetSection> sections;

        const auto* secs = lj::json::JGet(root, "sections");
        if (secs && secs->IsArr()) {
            for (const auto& s : secs->arr) {
                NetSection ns;
                ns.id = lj::json::JStr(s, "id");
                ns.name = net::FromUtf8(lj::json::JStr(s, "name"));
                ns.desc = net::FromUtf8(lj::json::JStr(s, "desc"));
                sections.push_back(std::move(ns));
            }
        }

        const auto* ps = lj::json::JGet(root, "posts");
        if (ps && ps->IsArr()) {
            for (const auto& v : ps->arr) {
                NetPost p;
                p.id = lj::json::JStr(v, "id");
                p.title = net::FromUtf8(lj::json::JStr(v, "title"));
                p.excerpt = net::FromUtf8(lj::json::JStr(v, "excerpt"));
                p.author = net::FromUtf8(lj::json::JStr(v, "author"));
                p.sectionId = net::FromUtf8(lj::json::JStr(v, "sectionId"));
                const auto* ca = lj::json::JGet(v, "createdAt");
                if (ca) p.createdAt = (long long)ca->num;
                const auto* ua = lj::json::JGet(v, "updatedAt");
                if (ua) p.updatedAt = (long long)ua->num;
                const auto* lc = lj::json::JGet(v, "likeCount");
                if (lc) p.likeCount = (int)lc->num;
                const auto* cc = lj::json::JGet(v, "commentCount");
                if (cc) p.commentCount = (int)cc->num;
                const auto* fc = lj::json::JGet(v, "favCount");
                if (fc) p.favCount = (int)fc->num;
                const auto* lk = lj::json::JGet(v, "liked");
                if (lk) p.liked = lk->bval;
                const auto* fv = lj::json::JGet(v, "faved");
                if (fv) p.faved = fv->bval;
                const auto* mn = lj::json::JGet(v, "mine");
                if (mn) p.mine = mn->bval;
                posts.push_back(std::move(p));
            }
        }

        {
            std::lock_guard<std::mutex> lk(m_cloudMu);
            m_srcPosts = std::move(posts);
            m_srcSections = std::move(sections);
            m_srcFetched = true;
        }
        m_cloudDirty.store(true);
    });
}

void RoadmapView::OnColChanged(const lj::json::JVal&)
{
    // 有人发帖 / 编辑 / 点赞 / 评论 → 重拉列表
    FetchPublicColumns();
}

void RoadmapView::SnapshotCloud()
{
    std::lock_guard<std::mutex> lk(m_cloudMu);
    m_pubPosts = m_srcPosts;
    m_pubSections = m_srcSections;
    m_pubLive = m_srcFetched && Cloud::Instance().Online();
}

} // namespace lj
