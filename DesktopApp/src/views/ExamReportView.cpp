// ============================================================
//  ExamReportView.cpp — F-D6 考场模式（模考报告 / 开始入口）
//  列表展示上次模考报告，提供开始模考的时长预设按钮；考试中显示状态 + 退出。
// ============================================================
#include "views/ExamReportView.h"
#include "core/ExamMode.h"
#include <ctime>

namespace lj {

bool ExamReportView::InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

std::wstring ExamReportView::FmtTime(long long ts)
{
    if (ts <= 0) return L"";
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_s(&tm, &t);
    wchar_t buf[64];
    swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d",
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

void ExamReportView::OnEnter()
{
    View::OnEnter();
    Reload();
    RecomputeLayout();
}

void ExamReportView::Reload()
{
    m_last = CheckinStore::Instance().LastExamReport();
    m_running = ExamMode::Instance().Running();
}

void ExamReportView::RecomputeLayout()
{
    if (m_cv) Layout(m_area, *m_cv);
}

void ExamReportView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    m_area = area;
    m_cv = &cv;

    float pad = 28.0f;
    float x = area.left + pad;
    float w = (area.right - area.left) - 2.0f * pad;

    ui::VLayout v(x, area.top + 100.0f, w, 16.0f);
    m_btnRects.clear();

    // ---- 卡片 1：开始模考 / 考试中 ----
    D2D1_RECT_F r1 = v.block(m_running ? 132.0f : 172.0f);
    m_card1 = r1;
    if (!m_running) {
        int n = (int)m_presets.size();
        float gap = 14.0f;
        float btnW = (w - gap * (n - 1) - 32.0f) / n;
        float by = r1.top + 100.0f;
        for (int i = 0; i < n; ++i) {
            float bx = r1.left + 16.0f + i * (btnW + gap);
            m_btnRects.push_back(ui::MakeRect(bx, by, btnW, 40.0f));
        }
    } else {
        m_stopRect = ui::MakeRect(r1.left + 16.0f, r1.top + 80.0f, 200.0f, 40.0f);
    }

    // ---- 卡片 2：上次模考报告 ----
    float h2 = (m_last.startTime > 0) ? 214.0f : 110.0f;
    D2D1_RECT_F r2 = v.block(h2);
    m_card2 = r2;

    SetContentHeight((v.bottom() - area.top) + 40.0f);
}

void ExamReportView::Update(float dt, const Input& in)
{
    if (m_toast) { m_toastT += dt; if (m_toastT > 2.4f) m_toast = false; }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    float mx = in.mouseX, my = shifted.mouseY;

    if (in.clicked) {
        if (m_running) {
            if (InRect(m_stopRect, mx, my)) {
                ExamMode::Instance().Stop();
                Reload();
                RecomputeLayout();
                Toast(L"已退出考场（标记放弃）");
                return;
            }
        } else {
            for (size_t i = 0; i < m_btnRects.size(); ++i) {
                if (InRect(m_btnRects[i], mx, my)) {
                    ExamMode::Instance().Start(m_presets[i]);
                    Reload();
                    RecomputeLayout();
                    Toast(std::to_wstring(m_presets[i]) + L" 分钟模考已开始");
                    return;
                }
            }
        }
    }
    View::Update(dt, in);
}

void ExamReportView::Toast(const std::wstring& msg)
{
    m_toastMsg = msg;
    m_toast = true;
    m_toastT = 0.0f;
}

void ExamReportView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 标题
    TextStyle ht; ht.size = 22.0f; ht.role = FontRole::Serif; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"考场 · 模考模式",
            { m_area.left + 28.0f, m_area.top + 24.0f, m_area.right - 28.0f, m_area.top + 60.0f }, ht, pal.ink900);
    TextStyle sub; sub.size = 12.0f; sub.role = FontRole::Sans;
    cv.Text(L"一键进入模考环境：全屏倒计时 + 中断检测，到点出专注报告。不锁系统、不强制关进程。",
            { m_area.left + 28.0f, m_area.top + 62.0f, m_area.right - 28.0f, m_area.top + 84.0f }, sub, pal.ink500);

    // ---- 卡片 1 ----
    cv.PaperCard(m_card1, 2.0f);
    TextStyle ct; ct.size = 15.0f; ct.role = FontRole::Sans; ct.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(m_running ? L"模考进行中" : L"开始模考",
            { m_card1.left + 16.0f, m_card1.top + 14.0f, m_card1.right - 16.0f, m_card1.top + 40.0f }, ct, pal.ink900);
    TextStyle ds; ds.size = 12.0f; ds.role = FontRole::Sans;
    if (!m_running) {
        cv.Text(L"选择模考时长，进入全屏倒计时。考试中切到娱乐 / 无关应用会记录一次中断。",
                { m_card1.left + 16.0f, m_card1.top + 46.0f, m_card1.right - 16.0f, m_card1.top + 70.0f }, ds, pal.ink500);
        for (size_t i = 0; i < m_btnRects.size(); ++i)
            PaintButton(cv, m_btnRects[i], std::to_wstring(m_presets[i]) + L" 分钟",
                        pal.paperDeep, pal.ink900);
    } else {
        cv.Text(L"考试进行中——顶部 HUD 显示剩余时间，右键托盘图标可退出考场。",
                { m_card1.left + 16.0f, m_card1.top + 46.0f, m_card1.right - 16.0f, m_card1.top + 70.0f }, ds, pal.ink500);
        PaintButton(cv, m_stopRect, L"退出考场（标记放弃）", pal.vermWash, pal.vermilion);
    }

    // ---- 卡片 2 ----
    cv.PaperCard(m_card2, 2.0f);
    if (m_last.startTime > 0) {
        PaintReport(cv, m_last, m_card2);
    } else {
        TextStyle es; es.size = 13.0f; es.role = FontRole::Sans;
        cv.Text(L"还没有模考记录，点上方按钮开始一次模考吧。",
                { m_card2.left + 16.0f, m_card2.top + 14.0f, m_card2.right - 16.0f, m_card2.top + 40.0f }, es, pal.ink500);
    }

    cv.PopTransform();
    cv.PopClip();

    if (m_toast) DrawToast(cv);
}

void ExamReportView::PaintReport(Canvas& cv, const ExamReport& r, const D2D1_RECT_F& box)
{
    const auto& pal = cv.Pal();
    float lx = box.left + 16.0f, rx = box.right - 16.0f;

    TextStyle ts; ts.size = 14.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"上次模考报告", { lx, box.top + 14.0f, rx, box.top + 38.0f }, ts, pal.ink900);

    auto fmtDur = [](int sec) {
        int m = sec / 60, s = sec % 60;
        return (m > 0 ? std::to_wstring(m) + L" 分 " + std::to_wstring(s) + L" 秒"
                      : std::to_wstring(s) + L" 秒");
    };

    TextStyle fs; fs.size = 13.0f; fs.role = FontRole::Sans;
    float y = box.top + 50.0f; const float lh = 26.0f;
    auto line = [&](const std::wstring& k, const std::wstring& val, const D2D1_COLOR_F& vc) {
        cv.Text(k, { lx, y, lx + 150.0f, y + 22.0f }, fs, pal.ink500);
        cv.Text(val, { lx + 150.0f, y, rx, y + 22.0f }, fs, vc);
        y += lh;
    };
    line(L"开始时间", FmtTime(r.startTime), pal.ink700);
    line(L"计划时长", std::to_wstring(r.plannedMin) + L" 分钟", pal.ink700);
    line(L"实际用时", fmtDur(r.actualSec), pal.ink700);
    line(L"有效专注", fmtDur(r.effectiveSec), pal.jade);
    line(L"中断次数", std::to_wstring(r.interrupts) + L" 次",
         r.interrupts > 0 ? pal.vermilion : pal.ink700);
    line(L"结果", r.abandoned ? L"中途放弃" : L"完成",
         r.abandoned ? pal.vermilion : pal.jade);

    // 完成度（有效 / 计划）
    int plannedSec = r.plannedMin * 60;
    int pct = plannedSec > 0 ? (int)(100LL * r.effectiveSec / plannedSec) : 0;
    TextStyle ps; ps.size = 12.0f; ps.role = FontRole::Sans;
    cv.Text(L"有效专注达成率 " + std::to_wstring(pct) + L"%",
            { lx, y, rx, y + 22.0f }, ps, pct >= 90 ? pal.jade : (pct >= 60 ? pal.brass : pal.vermilion));
}

void ExamReportView::PaintButton(Canvas& cv, const D2D1_RECT_F& r, const std::wstring& label,
                                const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg)
{
    cv.FillRoundRect(r, 6.0f, bg);
    TextStyle ts; ts.size = 13.0f; ts.role = FontRole::Sans;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(label, r, ts, fg);
}

void ExamReportView::DrawToast(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float w = 260.0f, h = 40.0f;
    float x = m_area.right - w - 28.0f;
    float y = m_area.bottom - h - 28.0f;
    D2D1_RECT_F r{ x, y, x + w, y + h };
    cv.FillRoundRect(r, 8.0f, pal.seal);
    TextStyle ts; ts.size = 13.0f; ts.role = FontRole::Sans;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(m_toastMsg, r, ts, pal.paperHi);
}

} // namespace lj
