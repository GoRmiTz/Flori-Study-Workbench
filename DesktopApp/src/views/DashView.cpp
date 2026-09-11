#include "views/DashView.h"
#include "app/Store.h"
#include "core/Hwnd.h"
#include "ui/Layout.h"
#include <cmath>
#include <cwchar>
#include <map>
#include <algorithm>

namespace lj {

const Date DashView::kHeatMin{ 2026, 7, 1 };

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
// 完成率 -> 等级（与网页 lvlOf 一致）
inline int LvlOfImpl(int p)
{
    if (p <= 0) return 0;
    if (p <= 25) return 1;
    if (p <= 50) return 2;
    if (p <= 75) return 3;
    return 4;
}

// 读取某日某打卡项是否完成：有记录取记录，无记录一律按未完成（诚实）
// 必须与 CheckinView 的勾选存储口径一致 —— 它按 CheckItem::Key() 落盘，
// 故优先按 Key() 匹配；旧数据可能按 title 存，再回退一次 title。
inline bool DoneOf(const std::map<std::wstring, bool>& dm, const CheckItem& it)
{
    auto f = dm.find(it.Key());
    if (f != dm.end()) return f->second;
    f = dm.find(it.title);
    if (f != dm.end()) return f->second;
    return false;
}

struct DayStat
{
    int done = 0, total = 0, pct = 0, mins = 0;
};
DayStat StatFor(const ChecklistBundle& b, const Date& d)
{
    auto dm = CheckinStore::Instance().LoadDay(FormatDate(d));
    DayStat r;
    for (const auto& it : ItemsForDate(b, d)) {
        bool done = DoneOf(dm, it);
        ++r.total;
        if (done) { ++r.done; r.mins += it.minutes; }
    }
    r.pct = r.total ? (int)((float)r.done / (float)r.total * 100.0f + 0.5f) : 0;
    return r;
}

int WebDayImpl(int z) { int w = (z + 4) % 7; if (w < 0) w += 7; return w; } // 0=周日
int DaysInMonthImpl(int y, int m)
{
    static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}
} // namespace

int DashView::WebDay(int z) { return WebDayImpl(z); }
int DashView::DaysInMonth(int y, int m) { return DaysInMonthImpl(y, m); }
void DashView::AddMonth(Date& d, int delta)
{
    int m = d.m + delta, y = d.y;
    y += (m - 1) / 12;
    m = ((m - 1) % 12 + 12) % 12 + 1;
    d.m = m; d.y = y; d.d = 1;
}
int DashView::LvlOf(int p) const { return LvlOfImpl(p); }

D2D1_COLOR_F DashView::LevelColor(const Palette& pal, int lvl) const
{
    switch (lvl) {
    case 0: return WithAlpha(pal.ink300, pal.dark ? 0.14f : 0.18f);
    case 1: return WithAlpha(pal.jade, 0.28f);
    case 2: return WithAlpha(pal.jade, 0.48f);
    case 3: return WithAlpha(pal.jade, 0.72f);
    default: return WithAlpha(pal.jade, 1.0f);
    }
}

// ============================================================
//  进入：计算所有统计 + 热力图数据
// ============================================================
void DashView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;

    // 与打卡页同源：自定义 items.json（无存档则回退 DefaultChecklist）
    m_bundle = CheckinStore::Instance().LoadItems();
    Date start{ 2026, 7, 29 };
    Date end = Today();
    m_totalMin = 0; m_trackedDays = 0; m_avg = 0; m_best = 0; m_perfectMonth = 0;

    int s = DaysFromCivil(start.y, start.m, start.d);
    int e = DaysFromCivil(end.y, end.m, end.d);
    for (int z = s; z <= e; ++z) {
        Date d = DateFromCivil(z);
        DayStat st = StatFor(m_bundle, d);
        m_totalMin += st.mins;
        if (st.done > 0) {
            ++m_trackedDays;
            m_avg += st.pct;
            if (st.pct > m_best) m_best = st.pct;
        }
        if (d.y == end.y && d.m == end.m && st.pct >= 100) ++m_perfectMonth;
    }
    m_avg = m_trackedDays ? m_avg / m_trackedDays : 0;

    // 专注统计
    auto fs = CheckinStore::Instance().LoadFocus();
    m_focusTotalMin = 0; m_focusCount = (int)fs.size();
    std::wstring tk = FormatDate(Today());
    m_todayFocusMin = 0;
    for (const auto& f : fs) {
        m_focusTotalMin += f.min;
        if (f.date == tk) m_todayFocusMin += f.min;
    }

    // 最近 14 天完成率
    m_recentPct.clear(); m_recentLbl.clear();
    for (int i = 13; i >= 0; --i) {
        Date d = DateFromCivil(e - i);
        m_recentPct.push_back(StatFor(m_bundle, d).pct);
        m_recentLbl.push_back(std::to_wstring(d.m) + L"/" + std::to_wstring(d.d));
    }

    // 专注趋势（近 14 天）
    m_focus14.clear(); m_focusLbl.clear(); m_focusMax = 60;
    for (int i = 13; i >= 0; --i) {
        std::wstring ds = FormatDate(DateFromCivil(e - i));
        int mins = 0;
        for (const auto& f : fs) if (f.date == ds) mins += f.min;
        m_focus14.push_back(mins);
        m_focusLbl.push_back(std::to_wstring(DateFromCivil(e - i).m) + L"/"
                             + std::to_wstring(DateFromCivil(e - i).d));
        if (mins > m_focusMax) m_focusMax = mins;
    }

    // 按科目累计时长
    m_subject.clear();
    std::vector<std::wstring> tags = { L"主线", L"英语", L"申论", L"常识", L"手绘", L"作息" };
    std::map<std::wstring, int> acc;
    for (int z = s; z <= e; ++z) {
        std::wstring key = FormatDate(DateFromCivil(z));
        auto dm = CheckinStore::Instance().LoadDay(key);
        for (const auto& it : ItemsForDate(m_bundle, DateFromCivil(z)))
            if (DoneOf(dm, it)) acc[it.tag] += it.minutes;
    }
    m_subjectMax = 1;
    for (const auto& tg : tags) {
        int v = acc[tg];
        m_subject.push_back({ tg, v });
        if (v > m_subjectMax) m_subjectMax = v;
    }

    // 热力图
    BuildYearData();
    m_dispMonth = Date{ end.y, end.m, 1 };
    BuildMonthData();

    // 作息 + 复盘
    BuildRhythm();
    m_journals = CheckinStore::Instance().LoadJournals();

    // #41 周报（近 7 天聚合 + 薄弱项）
    BuildWeekReport();
    m_weekWrote = false;
    m_weekWriteT = 0.0f;

    m_hasHover = false;
}

void DashView::OnLeave()
{
    View::OnLeave();
    if (m_jrOpen) CloseJournal(false);
    DestroyJournalEditors();
}

// ============================================================
//  作息汇总（近 30 天）
//  就寝时刻跨零点，用「+12 小时后取均值再还原」的环形均值，
//  否则 23:40 与 00:20 直接平均会得到中午 12 点这种荒唐结果。
// ============================================================
void DashView::BuildRhythm()
{
    m_rhWakeAvg = m_rhSleepAvg = m_rhDurAvg = -1;
    m_rhDays = 0;
    m_rhToday = DayRhythm{};

    auto all = CheckinStore::Instance().LoadRhythms();
    if (all.empty()) return;

    Date td = Today();
    int e = DaysFromCivil(td.y, td.m, td.d);
    long long wakeSum = 0, sleepShiftSum = 0, durSum = 0;
    int wakeN = 0, sleepN = 0, durN = 0;

    for (int i = 0; i < 30; ++i) {
        std::wstring key = FormatDate(DateFromCivil(e - i));
        auto f = all.find(key);
        if (f == all.end()) continue;
        const DayRhythm& r = f->second;
        ++m_rhDays;
        if (r.wake  >= 0) { wakeSum += r.wake; ++wakeN; }
        if (r.sleep >= 0) { sleepShiftSum += (r.sleep + 720) % 1440; ++sleepN; }
        if (r.wake >= 0 && r.sleep >= 0) {
            durSum += (r.wake - r.sleep + 1440) % 1440;
            ++durN;
        }
    }
    if (wakeN)  m_rhWakeAvg  = (int)(wakeSum / wakeN);
    if (sleepN) m_rhSleepAvg = (int)(((sleepShiftSum / sleepN) + 720) % 1440);
    if (durN)   m_rhDurAvg   = (int)(durSum / durN);

    auto ft = all.find(FormatDate(td));
    if (ft != all.end()) m_rhToday = ft->second;
}

std::wstring DashView::HHMM(int m)
{
    if (m < 0) return L"--:--";
    int h = (m / 60) % 24, mi = m % 60;
    wchar_t buf[8];
    swprintf(buf, 8, L"%02d:%02d", h, mi);
    return buf;
}

// ============================================================
//  热力图数据构建
// ============================================================
void DashView::BuildYearData()
{
    m_yearCells.clear();
    Date end = Today();
    int e = DaysFromCivil(end.y, end.m, end.d);
    int start = e - 364;
    start -= WebDayImpl(start);          // 对齐到周日

    int weeks = 53;
    for (int w = 0; w < weeks; ++w) {
        for (int wd = 0; wd < 7; ++wd) {
            int z = start + w * 7 + wd;
            if (z > e) { m_yearCells.push_back({ L"", 0, 0, 0, false, true }); continue; }
            Date d = DateFromCivil(z);
            DayStat st = StatFor(m_bundle, d);
            m_yearCells.push_back({ FormatDate(d), st.pct, st.done, st.total, false, false });
        }
    }
}

void DashView::BuildMonthData()
{
    m_monthCells.clear();
    int y = m_dispMonth.y, m = m_dispMonth.m;
    int startPad = WebDayImpl(DaysFromCivil(y, m, 1));
    int dim = DaysInMonth(y, m);
    for (int i = 0; i < startPad; ++i)
        m_monthCells.push_back({ L"", 0, 0, 0, false, true });
    Date today = Today();
    int te = DaysFromCivil(today.y, today.m, today.d);
    for (int d = 1; d <= dim; ++d) {
        int z = DaysFromCivil(y, m, d);
        DayStat st = StatFor(m_bundle, DateFromCivil(z));
        m_monthCells.push_back({ FormatDate(DateFromCivil(z)), st.pct, st.done, st.total,
                                 z > te, false });
    }
    while (m_monthCells.size() % 7 != 0)
        m_monthCells.push_back({ L"", 0, 0, 0, false, true });
}

// ============================================================
//  #41 F1 智能周报：本周（周一–周日）聚合 + 薄弱项识别（P1-6 对齐）
//  数据：checkin.json（打卡完成率，按 tag 归组）、focus.json（专注）、
//        journal.json（复盘天数）。纯本地规则，不接 AI。
//  薄弱项 = 实际完成率低于「方案文档目标正确率」的模块（见 PlanTargetPct）。
// ============================================================
// P1-6 方案文档目标正确率：各模块目标完成率，实际低于目标即薄弱项。
// 默认对每日主线的隐含要求；未知 tag 默认 80%。
static int PlanTargetPct(const std::wstring& tag)
{
    static const std::map<std::wstring, int> t = {
        { L"行测", 85 }, { L"申论", 80 }, { L"英语", 80 }, { L"专业课", 85 },
        { L"政治", 85 }, { L"时政", 75 }, { L"真题", 80 }, { L"模考", 80 },
        { L"错题", 75 }, { L"复盘", 90 }, { L"单词", 80 }, { L"专业课/政治", 85 },
    };
    auto it = t.find(tag);
    return it != t.end() ? it->second : 80;
}

void DashView::BuildWeekReport()
{
    m_weekTags.clear();
    m_weekAvg = m_weekDays = m_weekFocusMin = m_weekFocusCnt = m_weekJournal = 0;

    // 本周窗口：周一（含）至周日（含）
    Date td = Today();
    int wd = WebDayImpl(DaysFromCivil(td.y, td.m, td.d));   // 0=周日 … 6=周六
    int off = (wd + 6) % 7;                                  // 距本周一的天数
    int mon = DaysFromCivil(td.y, td.m, td.d) - off;
    int sun = mon + 6;
    int s = mon, e = sun;

    // 打卡：按 tag 聚合近 7 天 done/total
    std::map<std::wstring, std::pair<int, int>> acc;   // tag → {done, total}
    int sumPct = 0, nPct = 0;
    for (int z = s; z <= e; ++z) {
        Date d = DateFromCivil(z);
        DayStat st = StatFor(m_bundle, d);
        if (st.done > 0) ++m_weekDays;
        if (st.total > 0) { sumPct += st.pct; ++nPct; }
        std::wstring key = FormatDate(d);
        auto dm = CheckinStore::Instance().LoadDay(key);
        for (const auto& it : ItemsForDate(m_bundle, d)) {
            bool done = DoneOf(dm, it);
            auto& a = acc[it.tag];
            a.second += 1;
            if (done) a.first += 1;
        }
    }
    m_weekAvg = nPct ? sumPct / nPct : 0;

    for (const auto& kv : acc) {
        if (kv.second.second == 0) continue;
        int pct = (int)((float)kv.second.first / (float)kv.second.second * 100.0f + 0.5f);
        m_weekTags.push_back({ kv.first, pct, kv.second.second });
    }
    // 降序：完成率高的在前，薄弱项（最低）排最后
    std::sort(m_weekTags.begin(), m_weekTags.end(),
              [](const WeekItem& a, const WeekItem& b) { return a.pct > b.pct; });

    // 专注：近 7 天总分钟 / 次数
    std::wstring sKey = FormatDate(DateFromCivil(s)), eKey = FormatDate(DateFromCivil(e));
    for (const auto& f : CheckinStore::Instance().LoadFocus())
        if (f.date >= sKey && f.date <= eKey) { m_weekFocusMin += f.min; ++m_weekFocusCnt; }

    // 复盘：近 7 天有内容的天数
    for (const auto& j : CheckinStore::Instance().LoadJournals())
        if (j.first >= sKey && j.first <= eKey && !j.second.Empty()) ++m_weekJournal;
}

// ============================================================
//  布局
// ============================================================
void DashView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    const auto& C = Content::Get();
    (void)C;

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：每个区块只声明自身高度，位置由 flow 推进，杜绝手算坐标漂移
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // ---- 标题区 ----
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // ---- 统计卡（3×2）----
    m_cardRects.clear();
    float gap = 16.0f;
    float cardW = (contentW - gap * 2.0f) / 3.0f;
    float cardH = 140.0f;
    m_cardY = flow.block(34.0f + 2 * cardH + gap + 16.0f).top;
    for (int i = 0; i < 6; ++i) {
        int r = i / 3, c = i % 3;
        float cx = x0 + c * (cardW + gap);
        float cy = m_cardY + 34.0f + r * (cardH + gap);
        m_cardRects.push_back({ cx, cy, cx + cardW, cy + cardH });
    }

    // ---- 最近 14 天 ----
    m_barY = flow.block(34.0f + 16.0f + 96.0f + 24.0f).top;

    // ---- 年度热力图 ----
    m_yearY = flow.block(34.0f + 18.0f + 7 * 16.0f + 16.0f + 18.0f + 24.0f).top;

    // ---- 下部双栏（高度由左右两栏较低者决定，先量后占位）----
    m_lowerY = flow.cursorY;
    float colGap = 24.0f;
    float lcolW = contentW * 0.42f;
    float rcolW = contentW - lcolW - colGap;
    float lx = x0, rx = x0 + lcolW + colGap;

    // 左：月度热力图卡（格子按卡片宽自适应放大并居中填充，不再挤在左侧）
    m_monthCard = { lx, m_lowerY + 34.0f, lx + lcolW, 0 };
    {
        float innerW = lcolW - 32.0f;
        float gM = 4.0f;
        float cellM = (innerW - 6.0f * gM) / 7.0f;
        int rowsM = (int)((m_monthCells.size() + 6) / 7);
        if (rowsM < 1) rowsM = 1;
        float gridH = (float)rowsM * cellM + (float)(rowsM - 1) * gM;
        m_monthCard.bottom = m_monthCard.top + 30.0f + 16.0f + gridH + 16.0f + 18.0f + 8.0f;
    }

    // 右：专注趋势卡 + 科目卡
    m_focusCard = { rx, m_lowerY + 34.0f, rx + rcolW, 0 };
    float fh = 20.0f + 150.0f + 18.0f + 16.0f;
    m_focusCard.bottom = m_focusCard.top + fh;

    m_subjY = m_focusCard.bottom + 20.0f;
    m_subjCard = { rx, m_subjY, rx + rcolW, 0 };
    float sh = 30.0f + (float)m_subject.size() * 26.0f + 18.0f;
    m_subjCard.bottom = m_subjY + sh;

    float lowerBottom = (std::max)(m_monthCard.bottom, m_subjCard.bottom);
    flow.block(lowerBottom - m_lowerY + 24.0f);

    // ---- #41 本周周报（放作息复盘上方）----
    m_weekY = flow.block(34.0f + 128.0f + 26.0f).top;
    m_weekCard = { x0, m_weekY + 34.0f, x0 + contentW, m_weekY + 34.0f + 128.0f };

    // ---- 作息与复盘（卡片加高到 200，容纳 3 条复盘 + 底部「共 N 条」不重叠）----
    m_phY = flow.block(34.0f + 200.0f + 26.0f).top;

    // ---- #41 周报按钮（卡内右下）----
    m_weekBtn.label = m_weekWrote ? L"已写入明日三要事 ✓" : L"写入明日三要事";
    m_weekBtn.fontSize = 12.0f;
    m_weekBtn.bounds = { x0 + contentW - 170.0f, m_weekY + 34.0f + 128.0f - 44.0f,
                         x0 + contentW - 22.0f, m_weekY + 34.0f + 128.0f - 8.0f };

    // ---- 返回 ----
    m_backBtn.label = L"返 回 首 页";
    m_backBtn.tag = L"00";
    m_backBtn.fontSize = 13.0f;
    {
        D2D1_RECT_F bk = flow.block(46.0f + 30.0f);
        m_backBtn.bounds = { x0, bk.top, x0 + 150.0f, bk.top + 46.0f };
    }
    m_backBtn.onClick = [this] { Go(L"home"); };

    SetContentHeight(flow.cursorY - area.top);

    // 热力图格子矩形（依赖 contentW / x0，每帧刷新，成本低）
    float cell = 13.0f, g = 3.0f;
    // 年度
    m_yearRects.clear();
    float yx = x0 + 24.0f;
    float ytop = m_yearY + 34.0f + 18.0f;
    m_yearMonthLabels.clear();
    int lastMo = -1;
    for (size_t w = 0; w < 53; ++w) {
        Date wkStart = DateFromCivil((DaysFromCivil(Today().y, Today().m, Today().d) - 364)
                                     - WebDayImpl(DaysFromCivil(Today().y, Today().m, Today().d) - 364)
                                     + (int)w * 7);
        int mo = wkStart.m;
        if (mo != lastMo) {
            m_yearMonthLabels.push_back({ yx + (float)w * (cell + g) + (cell + g) * 0.5f,
                                          std::to_wstring(mo) + L"月" });
            lastMo = mo;
        }
        for (int rr = 0; rr < 7; ++rr) {
            size_t idx = w * 7 + rr;
            if (idx < m_yearCells.size())
                m_yearRects.push_back({ yx + (float)w * (cell + g), ytop + (float)rr * (cell + g),
                                       yx + (float)w * (cell + g) + cell,
                                       ytop + (float)rr * (cell + g) + cell });
            else
                m_yearRects.push_back({ 0,0,0,0 });
        }
    }
    // 月度（按卡片宽自适应放大，居中填充整个模块）
    m_monthRects.clear();
    {
        float cardW = m_monthCard.right - m_monthCard.left;
        float innerW = cardW - 32.0f;
        float gM = 4.0f;
        float cellM = (innerW - 6.0f * gM) / 7.0f;
        float gridW = 7.0f * cellM + 6.0f * gM;
        float mx = m_monthCard.left + (cardW - gridW) * 0.5f;
        float mtop = m_monthCard.top + 30.0f + 16.0f;
        for (size_t i = 0; i < m_monthCells.size(); ++i) {
            int col = (int)(i % 7), row = (int)(i / 7);
            m_monthRects.push_back({ mx + (float)col * (cellM + gM), mtop + (float)row * (cellM + gM),
                                     mx + (float)col * (cellM + gM) + cellM,
                                     mtop + (float)row * (cellM + gM) + cellM });
        }
    }
    // 月份导航按钮（置于月度卡顶部右侧）
    float nbW = 40.0f, nbH = 26.0f;
    m_prevBtn.label = L"‹";
    m_prevBtn.fontSize = 16.0f;
    m_prevBtn.bounds = { m_monthCard.right - 16.0f - nbW * 2 - 8.0f,
                         m_monthCard.top + 4.0f,
                         m_monthCard.right - 16.0f - nbW - 8.0f,
                         m_monthCard.top + 4.0f + nbH };
    m_nextBtn.label = L"›";
    m_nextBtn.fontSize = 16.0f;
    m_nextBtn.bounds = { m_monthCard.right - 16.0f - nbW,
                         m_monthCard.top + 4.0f,
                         m_monthCard.right - 16.0f,
                         m_monthCard.top + 4.0f + nbH };
    m_prevBtn.onClick = [this] {
        Date min = kHeatMin;
        if (m_dispMonth.y > min.y || (m_dispMonth.y == min.y && m_dispMonth.m > min.m)) {
            AddMonth(m_dispMonth, -1); BuildMonthData();
        }
    };
    m_nextBtn.onClick = [this] {
        Date now{ Today().y, Today().m, 1 };
        if (m_dispMonth.y < now.y || (m_dispMonth.y == now.y && m_dispMonth.m < now.m)) {
            AddMonth(m_dispMonth, 1); BuildMonthData();
        }
    };

    // 最近 14 天条矩形
    m_barRects.clear();
    float barAreaX = x0, barAreaW = contentW;
    float baseY = m_barY + 34.0f + 16.0f + 80.0f;
    float colW = barAreaW / 14.0f;
    for (int i = 0; i < 14; ++i) {
        float bx = barAreaX + colW * (i + 0.5f);
        m_barRects.push_back({ bx - colW * 0.32f, baseY - 80.0f, bx + colW * 0.32f, baseY });
    }

    // 专注趋势图框
    m_focusChartX = m_focusCard.left + 16.0f;
    m_focusChartY = m_focusCard.top + 34.0f;   // 下移，避开卡片标题（顶 12~28），否则 Y 轴标签压标题
    m_focusChartW = m_focusCard.right - m_focusChartX - 16.0f;
    m_focusChartH = 150.0f;

    // 科目条矩形
    m_subjRects.clear();
    float sxA = m_subjCard.left + 16.0f;
    float sw = m_subjCard.right - sxA - 16.0f;
    float sy = m_subjCard.top + 34.0f;
    for (size_t i = 0; i < m_subject.size(); ++i)
        m_subjRects.push_back({ sxA, sy + (float)i * 26.0f, sxA + sw, sy + (float)i * 26.0f + 16.0f });

    m_widgets.clear();
    m_widgets.push_back(&m_prevBtn);
    m_widgets.push_back(&m_nextBtn);
    m_widgets.push_back(&m_backBtn);
}

// ============================================================
//  更新
// ============================================================
void DashView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_jrA.Update(dt);

    // 复盘弹层激活：吞掉底层交互，只认弹层两个按钮
    if (m_jrOpen) {
        Input blocked = in;
        blocked.clicked = false; blocked.released = false; blocked.pressed = false;
        blocked.inWindow = false;
        UpdateWidgets(m_widgets, dt, blocked);
        m_hasHover = false;

        ComputeJournalRects();
        if (in.clicked) {
            const float mx = in.mouseX, my = in.mouseY;
            if (Hit(m_jrSave, mx, my))        CloseJournal(true);
            else if (Hit(m_jrCancel, mx, my)) CloseJournal(false);
            else if (!Hit(m_jrCard, mx, my))  CloseJournal(false);   // 点卡外取消
        }
        return;
    }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // #41 「写入明日三要事」：把薄弱项（完成率最低的 tag）写进今天复盘的 next
    if (m_weekWriteT > 0.0f) m_weekWriteT -= dt;
    if (m_weekWriteT <= 0.0f && m_weekWrote) m_weekWrote = false;
    m_weekBtn.label = m_weekWrote ? L"已写入明日三要事 ✓" : L"写入明日三要事";
    if (in.clicked) {
        const auto& wb = m_weekBtn.bounds;
        if (shifted.mouseX >= wb.left && shifted.mouseX <= wb.right &&
            shifted.mouseY >= wb.top && shifted.mouseY <= wb.bottom) {
            WriteWeakToJournal();
            return;
        }
    }

    // 「写今日复盘」入口
    if (in.clicked && Hit(m_jrEntry, shifted.mouseX, shifted.mouseY)) {
        OpenJournal();
        return;
    }

    // 悬停热力图格子
    m_hasHover = false;
    if (shifted.mouseX >= m_area.left && shifted.mouseX <= m_area.right) {
        for (size_t i = 0; i < m_yearRects.size(); ++i) {
            if (i < m_yearCells.size() && !m_yearCells[i].empty && !m_yearCells[i].future
                && (m_yearRects[i].right - m_yearRects[i].left) > 0.0f
                && m_yearRects[i].left <= shifted.mouseX && shifted.mouseX <= m_yearRects[i].right
                && m_yearRects[i].top <= shifted.mouseY && shifted.mouseY <= m_yearRects[i].bottom) {
                m_hover = m_yearCells[i]; m_hasHover = true;
                m_hoverX = in.mouseX; m_hoverY = in.mouseY; break;
            }
        }
        if (!m_hasHover) {
            for (size_t i = 0; i < m_monthRects.size(); ++i) {
                if (i < m_monthCells.size() && !m_monthCells[i].empty && !m_monthCells[i].future
                    && m_monthRects[i].left <= shifted.mouseX && shifted.mouseX <= m_monthRects[i].right
                    && m_monthRects[i].top <= shifted.mouseY && shifted.mouseY <= m_monthRects[i].bottom) {
                    m_hover = m_monthCells[i]; m_hasHover = true;
                    m_hoverX = in.mouseX; m_hoverY = in.mouseY; break;
                }
            }
        }
    }
}

// ============================================================
//  绘制
// ============================================================
void DashView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    PaintStatCards(cv, x0, m_cardY, contentW);
    PaintRecentBars(cv, x0, m_barY, contentW);
    PaintYearHeat(cv, x0, m_yearY, contentW);
    PaintLowerColumns(cv, x0, m_lowerY, contentW);
    PaintWeekReport(cv, x0, m_weekY, contentW);
    PaintRhythmJournal(cv, x0, m_phY, contentW);

    m_backBtn.Paint(cv);

    cv.PopTransform();
    cv.PopClip();

    // 悬停提示（屏幕坐标，不随滚动裁切）
    if (m_hasHover) PaintTooltip(cv);

    // 复盘弹层（屏幕坐标，与 Win32 EDIT 同一坐标系）
    PaintJournalOverlay(cv);

    // 滚动指示条
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

// ---------------- 标题 ----------------
void DashView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION · 全局执行数据", { x0, y, x0 + 320.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"仪表盘", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    float da = Clamp01((m_t - 0.25f) / 0.6f);
    if (da > 0.0f) {
        Date td = Today();
        TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.0f; ds.letterSpacing = 1.6f;
        ds.hAlign = HAlign::Right;
        cv.PushOpacity(ease::OutCubic(da));
        cv.Text(WeekdayCN(td) + L" · " + FormatDate(td),
                { x0 + contentW - 320.0f, y + 2.0f, x0 + contentW, y + 20.0f }, ds, pal.ink500);
        cv.PopOpacity();
    }
    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

// ---------------- 统计卡 ----------------
void DashView::PaintStatCards(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    (void)x0; (void)contentW;
    struct Card { std::wstring label, num, sub; };
    std::wstring h = (m_totalMin / 60 > 0) ? std::to_wstring(m_totalMin / 60) : L"0";
    std::wstring fh = (m_focusTotalMin / 60 > 0) ? std::to_wstring(m_focusTotalMin / 60) : L"0";
    Card cards[6] = {
        { L"总学习时长", h + L" h",  L"累计学习（小时）" },
        { L"专注总时长", fh + L" h", L"共 " + std::to_wstring(m_focusCount) + L" 次专注" },
        { L"今日专注",   std::to_wstring(m_todayFocusMin) + L" min", L"今日已记录" },
        { L"平均完成率", std::to_wstring(m_avg) + L" %", L"有记录日均值" },
        { L"最佳单日",   std::to_wstring(m_best) + L" %", L"历史最高" },
        { L"当月满勤",   std::to_wstring(m_perfectMonth) + L" 天", L"100% 完成日" },
    };
    for (int i = 0; i < 6 && (size_t)i < m_cardRects.size(); ++i) {
        float a = Clamp01((m_t - 0.30f - (float)i * 0.06f) / 0.5f);
        if (a <= 0.004f) continue;
        float er = ease::OutCubic(a);
        const auto& r = m_cardRects[i];
        cv.PushOpacity(er);
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - er) * 12.0f));

        cv.PaperCard(r, 0.0f, shape::kEdge);
        cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom }, WithAlpha(pal.seal, 0.8f));

        // 卡标题：放大并加深，用户一眼能看出这张卡是干什么的
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 14.0f; ls.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(cards[i].label, { r.left + 16.0f, r.top + 44.0f, r.right - 12.0f, r.top + 64.0f },
                ls, pal.ink900);

        TextStyle ns; ns.role = FontRole::Mono; ns.size = 26.0f; ns.weight = DWRITE_FONT_WEIGHT_BOLD;
        ns.tabularNums = true;
        cv.Text(cards[i].num, { r.left + 16.0f, r.top + 66.0f, r.right - 12.0f, r.top + 100.0f },
                ns, pal.ink900);

        TextStyle ss; ss.role = FontRole::Sans; ss.size = 10.5f;
        cv.Text(cards[i].sub, { r.left + 16.0f, r.top + 106.0f, r.right - 12.0f, r.bottom - 8.0f },
                ss, WithAlpha(pal.ink500, 0.92f));

        cv.PopTransform();
        cv.PopOpacity();
    }
}

// ---------------- 最近 14 天 ----------------
void DashView::PaintRecentBars(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.45f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 最近 14 天表现", { x0, y, x0 + 400.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x0 + 180.0f, x0 + contentW, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    float baseY = y + 34.0f + 16.0f + 80.0f;
    for (int i = 0; i < 14 && (size_t)i < m_barRects.size(); ++i) {
        float pct = (float)m_recentPct[i];
        float bh = (std::max)(3.0f, 80.0f * pct / 100.0f * e);
        const auto& r = m_barRects[i];
        D2D1_RECT_F track = { r.left, baseY - 80.0f, r.right, baseY };
        D2D1_RECT_F bar = { r.left, baseY - bh, r.right, baseY };
        cv.FillRoundRect(track, 2.0f, WithAlpha(pal.ink300, 0.10f));
        cv.FillRoundRect(bar, 2.0f, MixColor(pal.seal, pal.jade, pct / 100.0f));

        if (bh > 18.0f) {
            wchar_t buf[8];
            swprintf_s(buf, L"%d", (int)pct);
            TextStyle ps; ps.role = FontRole::Mono; ps.size = 9.5f;
            ps.hAlign = HAlign::Center; ps.tabularNums = true;
            cv.Text(buf, { r.left, baseY - bh - 14.0f, r.right, baseY - bh - 2.0f }, ps, pal.ink500);
        }
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 9.5f; ls.hAlign = HAlign::Center;
        cv.Text(m_recentLbl[i], { r.left - 6.0f, baseY + 4.0f, r.right + 6.0f, baseY + 20.0f },
                ls, pal.ink500);
    }
    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- 年度热力图 ----------------
void DashView::PaintYearHeat(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.55f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 年度打卡热力图（近 53 周）", { x0, y, x0 + 460.0f, y + 16.0f },
            sec, pal.ink300);
    cv.PerforationH(x0 + 280.0f, x0 + contentW, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    // 月份标签
    TextStyle ms; ms.role = FontRole::Mono; ms.size = 9.5f; ms.vAlign = VAlign::Middle;
    for (const auto& ml : m_yearMonthLabels)
        cv.Text(ml.second, { ml.first - 16.0f, m_yearY + 34.0f + 4.0f,
                             ml.first + 16.0f, m_yearY + 34.0f + 18.0f }, ms, pal.ink500);

    // 星期标签（一/三/五 行）
    static const wchar_t* dow[] = { L"", L"一", L"", L"三", L"", L"五", L"" };
    TextStyle ds; ds.role = FontRole::Mono; ds.size = 9.0f; ds.vAlign = VAlign::Middle;
    float ytop = m_yearY + 34.0f + 18.0f;
    for (int rr = 0; rr < 7; ++rr)
        if (dow[rr][0])
            cv.Text(dow[rr], { x0, ytop + (float)rr * 16.0f, x0 + 22.0f, ytop + (float)rr * 16.0f + 13.0f },
                    ds, pal.ink300);

    // 格子
    for (size_t i = 0; i < m_yearCells.size() && i < m_yearRects.size(); ++i) {
        if (m_yearCells[i].empty) continue;
        const auto& r = m_yearRects[i];
        D2D1_COLOR_F c = m_yearCells[i].future ? WithAlpha(pal.ink300, 0.06f)
                                                : LevelColor(pal, LvlOf(m_yearCells[i].pct));
        cv.FillRoundRect(r, 2.0f, c);
        if (m_hasHover && m_hover.date == m_yearCells[i].date)
            cv.StrokeRoundRect(r, 2.0f, pal.seal, 1.4f);
    }

    // 图例
    float ly = ytop + 7 * 16.0f + 12.0f;
    TextStyle lg; lg.role = FontRole::Sans; lg.size = 10.0f; lg.vAlign = VAlign::Middle;
    cv.Text(L"少", { x0 + 22.0f, ly, x0 + 44.0f, ly + 14.0f }, lg, pal.ink500);
    float lx = x0 + 46.0f;
    for (int l = 0; l < 5; ++l) {
        cv.FillRoundRect({ lx + (float)l * 16.0f, ly, lx + (float)l * 16.0f + 13.0f, ly + 13.0f },
                         2.0f, LevelColor(pal, l));
    }
    cv.Text(L"多", { lx + 5 * 16.0f + 4.0f, ly, lx + 5 * 16.0f + 30.0f, ly + 14.0f }, lg, pal.ink500);

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- 下部双栏 ----------------
void DashView::PaintLowerColumns(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.65f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    (void)x0; (void)contentW;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    // ===== 左：月度热力图 =====
    {
        cv.PaperCard(m_monthCard, 0.0f, shape::kEdge);
        TextStyle t1; t1.role = FontRole::Mono; t1.size = 10.5f; t1.letterSpacing = 2.2f;
        t1.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"月度打卡热力图", { m_monthCard.left + 16.0f, m_monthCard.top + 12.0f,
                                     m_monthCard.right - 110.0f, m_monthCard.top + 28.0f },
                t1, pal.ink300);
        // 月份标题（导航按钮之间）
        TextStyle mt; mt.role = FontRole::Mono; mt.size = 12.0f; mt.hAlign = HAlign::Center;
        mt.tabularNums = true;
        cv.Text(std::to_wstring(m_dispMonth.y) + L" 年 " + std::to_wstring(m_dispMonth.m) + L" 月",
                { m_monthCard.right - 110.0f, m_monthCard.top + 6.0f,
                  m_monthCard.right - 16.0f, m_monthCard.top + 30.0f }, mt, pal.ink700);
        m_prevBtn.Paint(cv);
        m_nextBtn.Paint(cv);

        // 星期表头
        static const wchar_t* hd[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
        TextStyle hs; hs.role = FontRole::Mono; hs.size = 9.5f; hs.hAlign = HAlign::Center; hs.vAlign = VAlign::Middle;
        float mx = m_monthCard.left + 16.0f;
        float mtop = m_monthCard.top + 30.0f + 16.0f;
        for (int c = 0; c < 7; ++c)
            cv.Text(hd[c], { mx + (float)c * 16.0f, mtop - 16.0f, mx + (float)c * 16.0f + 13.0f, mtop }, hs, pal.ink300);

        for (size_t i = 0; i < m_monthCells.size() && i < m_monthRects.size(); ++i) {
            const auto& r = m_monthRects[i];
            if (m_monthCells[i].empty) continue;
            D2D1_COLOR_F c = m_monthCells[i].future ? WithAlpha(pal.ink300, 0.06f)
                                                    : LevelColor(pal, LvlOf(m_monthCells[i].pct));
            cv.FillRoundRect(r, 2.0f, c);
            if (!m_monthCells[i].future) {
                TextStyle ps; ps.role = FontRole::Mono; ps.size = 8.0f; ps.hAlign = HAlign::Center; ps.vAlign = VAlign::Middle;
                Date d = DateFromCivil(DaysFromCivil(m_dispMonth.y, m_dispMonth.m, 1)
                                       + (int)(i - (int)WebDayImpl(DaysFromCivil(m_dispMonth.y, m_dispMonth.m, 1))));
                cv.Text(std::to_wstring(d.d), { r.left, r.top, r.right, r.bottom }, ps,
                        m_monthCells[i].pct >= 50 ? pal.paperHi : pal.ink300);
            }
            if (m_hasHover && m_hover.date == m_monthCells[i].date)
                cv.StrokeRoundRect(r, 2.0f, pal.seal, 1.4f);
        }
        // 图例
        float ly = m_monthCard.bottom - 22.0f;
        TextStyle lg; lg.role = FontRole::Sans; lg.size = 9.5f; lg.vAlign = VAlign::Middle;
        cv.Text(L"完成率：少", { m_monthCard.left + 16.0f, ly, m_monthCard.left + 70.0f, ly + 12.0f }, lg, pal.ink500);
        for (int l = 0; l < 5; ++l)
            cv.FillRoundRect({ m_monthCard.left + 74.0f + (float)l * 15.0f, ly,
                               m_monthCard.left + 74.0f + (float)l * 15.0f + 12.0f, ly + 12.0f },
                             2.0f, LevelColor(pal, l));
        cv.Text(L"多", { m_monthCard.left + 74.0f + 5 * 15.0f + 2.0f, ly,
                         m_monthCard.left + 74.0f + 5 * 15.0f + 24.0f, ly + 12.0f }, lg, pal.ink500);
    }

    // ===== 右：专注趋势 =====
    {
        cv.PaperCard(m_focusCard, 0.0f, shape::kEdge);
        TextStyle t1; t1.role = FontRole::Mono; t1.size = 10.5f; t1.letterSpacing = 2.2f;
        t1.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"专注时长趋势（近 14 天）", { m_focusCard.left + 16.0f, m_focusCard.top + 12.0f,
                                              m_focusCard.right - 16.0f, m_focusCard.top + 28.0f },
                t1, pal.ink300);

        // 揭示动画：从左到右裁切
        float reveal = Clamp01((m_t - 0.80f) / 0.6f);
        cv.PushClip({ m_focusChartX, m_focusChartY - 4.0f,
                      m_focusChartX + m_focusChartW * ease::OutCubic(reveal),
                      m_focusChartY + m_focusChartH + 4.0f });

        float padL = 30.0f, padR = 8.0f, padT = 8.0f, padB = 18.0f;
        float iw = m_focusChartW - padL - padR;
        float ih = m_focusChartH - padT - padB;
        float gx = m_focusChartX + padL, gy = m_focusChartY + padT;

        // 网格 + Y 轴标签
        TextStyle ax; ax.role = FontRole::Mono; ax.size = 9.0f; ax.hAlign = HAlign::Right; ax.vAlign = VAlign::Middle;
        for (int g = 0; g <= 4; ++g) {
            float gy2 = gy + ih - (ih / 4.0f) * g;
            cv.Line(gx, gy2, gx + iw, gy2, WithAlpha(pal.rule, 0.5f), 1.0f);
            int gv = (int)(m_focusMax - (m_focusMax / 4.0f) * g);
            cv.Text(std::to_wstring(gv), { gx - 4.0f, gy2 - 6.0f, gx - 4.0f, gy2 + 6.0f }, ax, pal.ink300);
        }
        // 面积（逐列竖条近似）+ 折线 + 点
        float step = m_focus14.size() > 1 ? iw / (m_focus14.size() - 1) : 0;
        std::vector<D2D1_POINT_2F> pts;
        for (size_t i = 0; i < m_focus14.size(); ++i) {
            float v = (float)m_focus14[i];
            float px = gx + (float)i * step;
            float py = gy + ih - (v / (float)m_focusMax) * ih;
            pts.push_back({ px, py });
            // 面积近似：从基线到点的竖条
            cv.FillRect({ px - step * 0.5f, py, px + step * 0.5f, gy + ih },
                        WithAlpha(pal.seal, 0.16f));
        }
        for (size_t i = 1; i < pts.size(); ++i)
            cv.Line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, pal.seal, 2.0f);
        for (size_t i = 0; i < pts.size(); ++i)
            cv.FillCircle(pts[i].x, pts[i].y, 2.6f, pal.seal);

        // X 轴标签（隔点）
        TextStyle xl; xl.role = FontRole::Mono; xl.size = 8.5f; xl.hAlign = HAlign::Center;
        for (size_t i = 0; i < m_focusLbl.size(); ++i) {
            if (i % 2 == 0 || m_focusLbl.size() <= 8)
                cv.Text(m_focusLbl[i], { gx + (float)i * step - 18.0f, gy + ih + 2.0f,
                                         gx + (float)i * step + 18.0f, gy + ih + 16.0f }, xl, pal.ink300);
        }
        cv.PopClip();
        // 单位
        TextStyle un; un.role = FontRole::Mono; un.size = 9.0f; un.vAlign = VAlign::Top;
        cv.Text(L"单位：分钟", { m_focusChartX, m_focusCard.bottom - 16.0f,
                                 m_focusChartX + 120.0f, m_focusCard.bottom - 4.0f }, un, pal.ink500);
    }

    // ===== 右：按科目累计 =====
    {
        cv.PaperCard(m_subjCard, 0.0f, shape::kEdge);
        TextStyle t1; t1.role = FontRole::Mono; t1.size = 10.5f; t1.letterSpacing = 2.2f;
        t1.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"按科目累计时长", { m_subjCard.left + 16.0f, m_subjCard.top + 12.0f,
                                     m_subjCard.right - 16.0f, m_subjCard.top + 28.0f }, t1, pal.ink300);

        D2D1_COLOR_F tagCols[6] = { pal.seal, pal.jade, pal.brass, pal.brass, pal.jade, pal.ink500 };
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.vAlign = VAlign::Middle;
        TextStyle vs; vs.role = FontRole::Mono; vs.size = 10.5f; vs.hAlign = HAlign::Right; vs.vAlign = VAlign::Middle;
        vs.tabularNums = true;
        for (size_t i = 0; i < m_subject.size() && i < m_subjRects.size(); ++i) {
            const auto& r = m_subjRects[i];
            D2D1_COLOR_F c = (i < 6) ? tagCols[i] : pal.seal;
            cv.Text(m_subject[i].first, { r.left, r.top - 2.0f, r.left + 92.0f, r.bottom + 2.0f }, ls, pal.ink700);
            float tw = (r.right - 58.0f) - (r.left + 98.0f);
            float fillW = (m_subjectMax > 0) ? tw * (float)m_subject[i].second / (float)m_subjectMax : 0.0f;
            D2D1_RECT_F track = { r.left + 98.0f, r.top, r.right - 58.0f, r.bottom };
            D2D1_RECT_F fill = { r.left + 98.0f, r.top, r.left + 98.0f + (std::max)(fillW, 1.0f), r.bottom };
            cv.FillRoundRect(track, 3.0f, WithAlpha(pal.ink300, 0.10f));
            if (m_subject[i].second > 0)
                cv.FillRoundRect(fill, 3.0f, WithAlpha(c, 0.85f));
            cv.Text(std::to_wstring(m_subject[i].second) + L" 分", { r.right - 56.0f, r.top, r.right, r.bottom }, vs, pal.ink500);
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- #41 F1 智能周报 ----------------
void DashView::PaintWeekReport(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.65f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 本周周报", { x0, y, x0 + 460.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x0 + 160.0f, x0 + contentW, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_weekCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_weekCard, shape::kEdge, pal.rule, shape::kHair);

    float ix = m_weekCard.left + 26.0f;
    float right = m_weekCard.right - 26.0f;
    float cy = m_weekCard.top + 18.0f;

    // 三组关键数字
    auto stat = [&](const wchar_t* label, int value, const wchar_t* unit, float cx, float w) {
        TextStyle vs; vs.role = FontRole::Mono; vs.size = 24.0f; vs.vAlign = VAlign::Middle;
        vs.tabularNums = true; vs.weight = DWRITE_FONT_WEIGHT_BOLD;
        std::wstring txt = std::to_wstring(value) + unit;
        cv.Text(txt, { cx, cy, cx + w, cy + 34.0f }, vs, pal.ink900);
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.5f;
        cv.Text(label, { cx, cy + 34.0f, cx + w, cy + 54.0f }, ls, pal.ink500);
    };
    stat(L"本周平均完成率", m_weekAvg, L"%", ix, 200.0f);
    stat(L"专注总时长（分钟）", m_weekFocusMin, L"", ix + 210.0f, 180.0f);
    stat(L"专注次数", m_weekFocusCnt, L"", ix + 400.0f, 140.0f);
    stat(L"复盘天数", m_weekJournal, L"", ix + 560.0f, 140.0f);

    // 薄弱项：实际完成率 < 方案文档目标正确率（按缺口降序）
    float wy = m_weekCard.top + 78.0f;
    TextStyle ws; ws.role = FontRole::Sans; ws.size = 12.5f; ws.vAlign = VAlign::Middle;
    cv.Text(L"薄弱项：", { ix, wy, ix + 70.0f, wy + 22.0f }, ws, pal.ink700);
    struct WeakRef { const WeekItem* t; int gap; };
    std::vector<WeakRef> weak;
    for (const auto& t : m_weekTags) {
        int target = PlanTargetPct(t.tag);
        if (t.pct < target) weak.push_back({ &t, target - t.pct });
    }
    std::sort(weak.begin(), weak.end(), [](const WeakRef& a, const WeakRef& b) { return a.gap > b.gap; });
    if (weak.empty()) {
        cv.Text(L"本周各模块均达方案目标，暂无薄弱项。保持节奏即可。",
                { ix + 70.0f, wy, right - 190.0f, wy + 22.0f }, ws, pal.jade);
    } else {
        std::wstring s;
        for (size_t i = 0; i < weak.size() && i < 3; ++i) {
            if (i) s += L" · ";
            int target = PlanTargetPct(weak[i].t->tag);
            s += weak[i].t->tag + L" " + std::to_wstring(weak[i].t->pct) + L"%"
               + L"（目标" + std::to_wstring(target) + L"%）";
        }
        cv.Text(s, { ix + 70.0f, wy, right - 190.0f, wy + 22.0f }, ws, pal.seal);
    }

    // 写入明日三要事按钮
    m_weekBtn.Paint(cv);

    cv.PopOpacity();
    cv.PopTransform();
}

void DashView::WriteWeakToJournal()
{
    // 薄弱项：实际完成率 < 方案文档目标，按缺口（目标−实际）降序取前 3
    std::vector<WeekItem> weak;
    for (const auto& t : m_weekTags) {
        int target = PlanTargetPct(t.tag);
        if (t.pct < target) weak.push_back(t);
    }
    std::sort(weak.begin(), weak.end(), [](const WeekItem& a, const WeekItem& b) {
        return (PlanTargetPct(a.tag) - a.pct) > (PlanTargetPct(b.tag) - b.pct);
    });

    std::wstring key = FormatDate(Today());
    DayJournal j = CheckinStore::Instance().LoadJournal(key);

    std::wstring next;
    if (weak.empty()) {
        next = L"1. 保持本周节奏，继续推进主线。";
    } else {
        for (size_t i = 0; i < weak.size() && i < 3; ++i) {
            int target = PlanTargetPct(weak[i].tag);
            next += std::to_wstring(i + 1) + L". 补强 " + weak[i].tag
                  + L"（方案目标 " + std::to_wstring(target) + L"% · 本周实际 "
                  + std::to_wstring(weak[i].pct) + L"%）。\n";
        }
    }
    j.next = next;
    CheckinStore::Instance().SaveJournal(key, j);

    m_weekWrote = true;
    m_weekWriteT = 2.5f;
}

// ---------------- 作息环形图 + 历史小结 ----------------
void DashView::PaintRhythmJournal(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.75f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 作息与复盘", { x0, y, x0 + 460.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x0 + 200.0f, x0 + contentW, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    const float gap = 16.0f, ch = 200.0f;
    const float cw = (contentW - gap) / 2.0f;
    D2D1_RECT_F c1{ x0, y + 34.0f, x0 + cw, y + 34.0f + ch };
    D2D1_RECT_F c2{ x0 + cw + gap, y + 34.0f, x0 + cw + gap + cw, y + 34.0f + ch };
    cv.PaperCard(c1, 0.0f, shape::kEdge);
    cv.PaperCard(c2, 0.0f, shape::kEdge);
    cv.StrokeRoundRect(c1, shape::kEdge, WithAlpha(pal.rule, 0.6f), shape::kHair);
    cv.StrokeRoundRect(c2, shape::kEdge, WithAlpha(pal.rule, 0.6f), shape::kHair);

    TextStyle ct; ct.role = FontRole::Sans; ct.size = 13.5f; ct.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    TextStyle nt; nt.role = FontRole::Sans; nt.size = 11.0f; nt.vAlign = VAlign::Top;
    TextStyle mn; mn.role = FontRole::Mono; mn.size = 10.0f; mn.letterSpacing = 0.6f;

    // ============ 左：24 小时作息刻度盘 ============
    cv.Text(L"作息环形图（近 30 天）",
            { c1.left + 16.0f, c1.top + 14.0f, c1.right - 16.0f, c1.top + 36.0f }, ct, pal.ink700);

    const float R = 52.0f;
    const float ccx = c1.left + 16.0f + R + 6.0f;
    const float ccy = c1.top + 40.0f + R + 8.0f;
    const bool  hasRh = (m_rhWakeAvg >= 0 || m_rhSleepAvg >= 0);

    // 96 根 15 分钟刻度；落在「就寝→起床」区间内的加深，表示睡眠
    for (int k = 0; k < 96; ++k) {
        const int minute = k * 15;
        const float ang = (float)minute / 1440.0f * 6.2831853f - 1.5707963f;
        const bool major = (minute % 360 == 0);        // 0 / 6 / 12 / 18 点
        bool inSleep = false;
        if (m_rhSleepAvg >= 0 && m_rhWakeAvg >= 0) {
            const int rel  = (minute - m_rhSleepAvg + 1440) % 1440;
            const int span = (m_rhWakeAvg - m_rhSleepAvg + 1440) % 1440;
            inSleep = (rel < span);
        }
        const float r0 = R - (major ? 16.0f : (inSleep ? 12.0f : 7.0f));
        const float r1 = R;
        D2D1_COLOR_F c = major ? WithAlpha(pal.ink500, 0.85f)
                       : inSleep ? WithAlpha(pal.seal, 0.72f)
                                 : WithAlpha(pal.ink300, 0.30f);
        cv.Line(ccx + cosf(ang) * r0, ccy + sinf(ang) * r0,
                ccx + cosf(ang) * r1, ccy + sinf(ang) * r1, c, major ? 1.6f : 1.1f);
    }
    cv.StrokeCircle(ccx, ccy, R + 5.0f, WithAlpha(pal.rule, 0.7f), shape::kHair);

    // 起床 / 就寝指针
    auto Hand = [&](int minute, const D2D1_COLOR_F& c) {
        if (minute < 0) return;
        const float ang = (float)minute / 1440.0f * 6.2831853f - 1.5707963f;
        cv.Line(ccx, ccy, ccx + cosf(ang) * (R - 18.0f), ccy + sinf(ang) * (R - 18.0f), c, 1.8f);
        cv.FillCircle(ccx + cosf(ang) * (R - 18.0f), ccy + sinf(ang) * (R - 18.0f), 3.0f, c);
    };
    Hand(m_rhSleepAvg, WithAlpha(pal.ink700, 0.9f));
    Hand(m_rhWakeAvg,  pal.vermilion);
    cv.FillCircle(ccx, ccy, 2.6f, WithAlpha(pal.ink700, 0.9f));

    // 中心：平均睡眠时长
    {
        TextStyle cs; cs.role = FontRole::Mono; cs.size = 11.0f;
        cs.hAlign = HAlign::Center; cs.vAlign = VAlign::Middle;
        cs.weight = DWRITE_FONT_WEIGHT_BOLD;
        std::wstring dur = L"--";
        if (m_rhDurAvg >= 0)
            dur = std::to_wstring(m_rhDurAvg / 60) + L"h" + std::to_wstring(m_rhDurAvg % 60) + L"m";
        cv.Text(dur, { ccx - R, ccy + 14.0f, ccx + R, ccy + 30.0f }, cs, pal.ink500);
    }

    // 右侧读数
    {
        const float lx = ccx + R + 20.0f;
        float ly = c1.top + 46.0f;
        auto Row = [&](const wchar_t* k, const std::wstring& v, const D2D1_COLOR_F& vc) {
            cv.Text(k, { lx, ly, lx + 56.0f, ly + 16.0f }, mn, pal.ink300);
            TextStyle vs; vs.role = FontRole::Mono; vs.size = 14.0f;
            vs.weight = DWRITE_FONT_WEIGHT_BOLD; vs.tabularNums = true;
            cv.Text(v, { lx + 54.0f, ly - 2.0f, c1.right - 14.0f, ly + 18.0f }, vs, vc);
            ly += 26.0f;
        };
        Row(L"就寝", HHMM(m_rhSleepAvg), pal.ink700);
        Row(L"起床", HHMM(m_rhWakeAvg), pal.vermilion);
        Row(L"睡眠", m_rhDurAvg >= 0
                     ? (std::to_wstring(m_rhDurAvg / 60) + L" 小时 " + std::to_wstring(m_rhDurAvg % 60) + L" 分")
                     : L"--", pal.ink700);
        Row(L"样本", std::to_wstring(m_rhDays) + L" 天", pal.ink500);

        if (!hasRh) {
            TextStyle hs; hs.role = FontRole::Sans; hs.size = 10.5f; hs.vAlign = VAlign::Top;
            cv.Text(L"勾选含「早起 / 睡觉」的打卡项即自动记录时刻。",
                    { lx, ly + 2.0f, c1.right - 14.0f, c1.bottom - 10.0f }, hs, pal.ink300);
        }
    }

    // ============ 右：历史小结 ============
    cv.Text(L"历史小结（每日复盘）",
            { c2.left + 16.0f, c2.top + 14.0f, c2.right - 16.0f, c2.top + 36.0f }, ct, pal.ink700);

    // 「写今日复盘」入口
    const std::wstring todayKey = FormatDate(Today());
    bool hasToday = false;
    for (const auto& kv : m_journals) if (kv.first == todayKey) { hasToday = true; break; }
    m_jrEntry = { c2.right - 16.0f - 104.0f, c2.top + 12.0f, c2.right - 16.0f, c2.top + 34.0f };
    cv.FillRoundRect(m_jrEntry, 5.0f, hasToday ? WithAlpha(pal.ink300, 0.14f) : pal.seal);
    cv.StrokeRoundRect(m_jrEntry, 5.0f, hasToday ? pal.rule : pal.seal, shape::kHair);
    {
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 11.0f;
        bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
        cv.Text(hasToday ? L"编辑今日复盘" : L"写今日复盘", m_jrEntry, bs,
                hasToday ? pal.ink700 : pal.paperHi);
    }

    if (m_journals.empty()) {
        cv.Text(L"还没有任何复盘。每天收工前写两行——今天做成了什么、明天最要紧的三件事，"
                L"一周后回头看，比任何完成率都诚实。",
                { c2.left + 16.0f, c2.top + 46.0f, c2.right - 16.0f, c2.bottom - 14.0f },
                nt, pal.ink500);
    } else {
        float jy = c2.top + 44.0f;
        int shown = 0;
        for (auto it = m_journals.rbegin(); it != m_journals.rend() && shown < 3; ++it, ++shown) {
            if (jy > c2.bottom - 30.0f) break;
            // 日期签
            std::wstring d = it->first.size() >= 10 ? it->first.substr(5) : it->first;
            cv.Text(d, { c2.left + 16.0f, jy, c2.left + 60.0f, jy + 14.0f }, mn, pal.seal);
            TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.vAlign = VAlign::Top;
            std::wstring body = it->second.summary.empty() ? it->second.next : it->second.summary;
            for (auto& ch2 : body) if (ch2 == L'\n' || ch2 == L'\r') ch2 = L' ';
            if (body.size() > 46) body = body.substr(0, 46) + L"…";
            cv.Text(body, { c2.left + 62.0f, jy, c2.right - 16.0f, jy + 36.0f }, ls, pal.ink700);
            jy += 42.0f;
            if (shown < 2) cv.Line(c2.left + 16.0f, jy - 12.0f, c2.right - 16.0f, jy - 12.0f,
                                   WithAlpha(pal.rule, 0.5f), shape::kHair);
        }
        TextStyle cs; cs.role = FontRole::Mono; cs.size = 9.5f; cs.letterSpacing = 1.2f;
        cv.Text(L"共 " + std::to_wstring(m_journals.size()) + L" 条复盘",
                { c2.left + 16.0f, c2.bottom - 22.0f, c2.right - 16.0f, c2.bottom - 8.0f },
                cs, pal.ink300);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ============================================================
//  复盘编辑弹层（Win32 EDIT 承载中文 IME）
// ============================================================
void DashView::ComputeJournalRects()
{
    const float cx = (m_area.left + m_area.right) * 0.5f;
    const float w = (std::min)(520.0f, m_area.right - m_area.left - 80.0f);
    const float h = 336.0f;
    const float x = cx - w / 2.0f;
    const float yy = m_area.top + 70.0f;
    m_jrCard = { x, yy, x + w, yy + h };
    m_jrBox1 = { x + 24.0f, yy + 84.0f,  x + w - 24.0f, yy + 168.0f };
    m_jrBox2 = { x + 24.0f, yy + 196.0f, x + w - 24.0f, yy + 268.0f };
    const float bw = 132.0f;
    m_jrSave   = { x + w - 24.0f - bw, yy + h - 56.0f, x + w - 24.0f, yy + h - 18.0f };
    m_jrCancel = { x + w - 36.0f - bw * 2.0f, yy + h - 56.0f, x + w - 36.0f - bw, yy + h - 18.0f };
}

void DashView::EnsureJournalEditors()
{
    HWND parent = AppHwnd();
    if (!parent) return;
    if (!m_edFont) {
        m_edFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");
    }
    auto mk = [&](HWND& h) {
        if (h) return;
        h = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE
                            | ES_AUTOVSCROLL | ES_LEFT | ES_WANTRETURN,
                            0, 0, 10, 10, parent, nullptr,
                            (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
        if (h && m_edFont) SendMessageW(h, WM_SETFONT, (WPARAM)m_edFont, TRUE);
        if (h) ShowWindow(h, SW_HIDE);
    };
    mk(m_ed1);
    mk(m_ed2);
}

void DashView::DestroyJournalEditors()
{
    if (m_ed1) { DestroyWindow(m_ed1); m_ed1 = nullptr; }
    if (m_ed2) { DestroyWindow(m_ed2); m_ed2 = nullptr; }
    if (m_edFont) { DeleteObject(m_edFont); m_edFont = nullptr; }
}

void DashView::OpenJournal()
{
    const std::wstring key = FormatDate(Today());
    m_jrDraft = CheckinStore::Instance().LoadJournal(key);
    m_jrOpen = true;
    m_jrA.target = 1.0f;

    ComputeJournalRects();
    EnsureJournalEditors();
    // 日记框落在复盘卡（lift=0.4）上，背景与其面色一致 → 无缝
    lj::SetEditBackdrop(lj::CardFace(0.4f));
    const float s = (float)AppDpi() / 96.0f;
    auto place = [&](HWND h, const D2D1_RECT_F& r, const std::wstring& text) {
        if (!h) return;
        SetWindowTextW(h, text.c_str());
        SetWindowPos(h, nullptr, (int)(r.left * s), (int)(r.top * s),
                     (int)((r.right - r.left) * s), (int)((r.bottom - r.top) * s), SWP_NOZORDER);
        ShowWindow(h, SW_SHOW);
    };
    place(m_ed1, m_jrBox1, m_jrDraft.summary);
    place(m_ed2, m_jrBox2, m_jrDraft.next);
    if (m_ed1) SetFocus(m_ed1);
}

void DashView::CloseJournal(bool save)
{
    auto grab = [](HWND h) -> std::wstring {
        if (!h) return L"";
        int n = GetWindowTextLengthW(h);
        std::wstring t; t.resize((size_t)n + 1);
        GetWindowTextW(h, &t[0], n + 1);
        t.resize((size_t)n);
        return t;
    };
    if (save) {
        DayJournal j;
        j.summary = grab(m_ed1);
        j.next    = grab(m_ed2);
        CheckinStore::Instance().SaveJournal(FormatDate(Today()), j);
        m_journals = CheckinStore::Instance().LoadJournals();
    }
    if (m_ed1) ShowWindow(m_ed1, SW_HIDE);
    if (m_ed2) ShowWindow(m_ed2, SW_HIDE);
    if (AppHwnd()) SetFocus(AppHwnd());
    m_jrOpen = false;
    m_jrA.target = 0.0f;
}

void DashView::PaintJournalOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    const float a = m_jrA.value;
    if (a <= 0.004f) return;
    ComputeJournalRects();

    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.6f * a));
    cv.PushOpacity(a);
    cv.PaperCard(m_jrCard, 0.4f, shape::kEdge);
    cv.DoubleFrame(m_jrCard, WithAlpha(pal.seal, 0.8f));

    TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 21.0f;
    ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.6f;
    cv.Text(L"今日复盘 · " + FormatDate(Today()),
            { m_jrCard.left + 26.0f, m_jrCard.top + 20.0f,
              m_jrCard.right - 26.0f, m_jrCard.top + 52.0f }, ttl, pal.ink900);

    TextStyle hs; hs.role = FontRole::Mono; hs.size = 10.5f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"今日小结", { m_jrBox1.left, m_jrBox1.top - 20.0f, m_jrBox1.right, m_jrBox1.top - 4.0f },
            hs, pal.seal);
    cv.Text(L"明日三要事", { m_jrBox2.left, m_jrBox2.top - 20.0f, m_jrBox2.right, m_jrBox2.top - 4.0f },
            hs, pal.seal);

    // EDIT 控件由 Win32 绘制，这里只描边给出纸面框感
    cv.StrokeRoundRect({ m_jrBox1.left - 3.0f, m_jrBox1.top - 3.0f,
                         m_jrBox1.right + 3.0f, m_jrBox1.bottom + 3.0f },
                       4.0f, WithAlpha(pal.rule, 0.9f), shape::kHair);
    cv.StrokeRoundRect({ m_jrBox2.left - 3.0f, m_jrBox2.top - 3.0f,
                         m_jrBox2.right + 3.0f, m_jrBox2.bottom + 3.0f },
                       4.0f, WithAlpha(pal.rule, 0.9f), shape::kHair);

    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary) {
        cv.FillRoundRect(r, 6.0f, primary ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(r, 6.0f, primary ? pal.seal : pal.rule, shape::kHair);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f;
        bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(label, r, bs, primary ? pal.paperHi : pal.ink700);
    };
    Btn(m_jrCancel, L"取消", false);
    Btn(m_jrSave,   L"存入档案", true);

    cv.PopOpacity();
}

// ---------------- 悬停提示 ----------------
void DashView::DebugForceOpen()
{
    if (!m_jrOpen) OpenJournal();
}

void DashView::PaintTooltip(Canvas& cv)
{
    const auto& pal = cv.Pal();
    if (m_hover.empty || m_hover.date.empty()) return;
    std::wstring tip = m_hover.date + L"  完成 " + std::to_wstring(m_hover.done) + L"/"
                       + std::to_wstring(m_hover.total) + L"（" + std::to_wstring(m_hover.pct) + L"%）";
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 11.0f;
    float tw = cv.MeasureWidth(tip, ts) + 24.0f;
    float th = 26.0f;
    float tx = m_hoverX + 14.0f;
    float ty = m_hoverY + 14.0f;
    if (tx + tw > m_area.right) tx = m_hoverX - tw - 14.0f;
    if (ty + th > m_area.bottom) ty = m_hoverY - th - 14.0f;
    D2D1_RECT_F r{ tx, ty, tx + tw, ty + th };
    cv.PaperCard(r, 0.0f, shape::kEdgeSoft);
    cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom }, WithAlpha(pal.seal, 0.85f));
    cv.Text(tip, { r.left + 12.0f, r.top, r.right - 6.0f, r.bottom }, ts, pal.ink900);
}

} // namespace lj
