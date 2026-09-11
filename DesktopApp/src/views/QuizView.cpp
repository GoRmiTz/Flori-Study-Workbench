// ============================================================
//  QuizView.cpp — 练考模块 Phase 1（阅读器 + 答题卡）
//  列表（扫描 accounts/<账户>/quiz/<类别>/*.md）→ 居中试卷弹层
//  （仿专栏详情），左侧题目 + 右侧答题卡；提交判分 → 解析展开。
//  无 AI 依赖即可运行（用手写 .md / 内置样例验证）。
// ============================================================
#include "views/QuizView.h"
#include "core/ExamMode.h"   // 进入全屏考场模考：ExamMode::Instance().Start(120)
#include "app/AccountStore.h"
#include "quiz/QuizScheduler.h"
#include "quiz/QuizStore.h"
#include "ui/Layout.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <ctime>

namespace lj {

QuizView::QuizView()
{
    // Phase 2-3：随视图构造（应用启动即构建）启动每日定时出题调度器。
    // 调度器挂 message-only 窗口 + 60s 定时器，主消息循环负责泵送 WM_TIMER；
    // 后台线程完成 AI 出题与落盘，不阻塞主线程。
    QuizScheduler::Instance().Init();
}

// 秒 → mm:ss
std::wstring QuizView::FmtMMSS(int sec)
{
    if (sec < 0) sec = 0;
    int m = sec / 60, s = sec % 60;
    wchar_t buf[16];
    swprintf_s(buf, 16, L"%02d:%02d", m, s);
    return buf;
}

// 解析时长：取首个连续数字串，"90" / "90 分钟" / "约90min" → 90
int QuizView::ParseDurationMin(const std::wstring& s)
{
    std::wstring digits;
    for (wchar_t c : s) {
        if (c >= L'0' && c <= L'9') digits.push_back(c);
        else if (!digits.empty()) break;   // 数字后遇非数字即停
    }
    if (digits.empty()) return 0;
    int v = 0; for (wchar_t c : digits) v = v * 10 + (c - L'0');
    return v;
}

// 内置样例卷（UTF-8，写入 quiz/时政/ 供首次试用，符合《题目MD规范》）
static const char* kSampleQuizUtf8 = R"QUIZ(---
title: 2026-08-15 时政每日一练
date: 2026-08-15
category: 时政
mode: quiz
source: 内置样例
model: none
---

# 2026-08-15 时政每日一练

> 本卷为内置样例，验证《题目MD规范》解析；提交后显示答案与解析。

## Q1
**题干**：2026 年中央一号文件聚焦的主题是？
- A. 乡村振兴与粮食安全
- B. 科技创新引领新质生产力
- C. 生态文明与绿色发展
- D. 扩大内需与消费升级
**题型**：单选
**答案**：A
**解析**：2026 年中央一号文件继续把推进乡村全面振兴作为主题，并把粮食安全摆在突出位置。
**知识点**：中央一号文件
**难度**：1

## Q2
**题干**：下列哪些属于 2026 年政府工作报告提出的重点工作任务？（多选）
- A. 因地制宜发展新质生产力
- B. 深入推进生态文明建设和绿色低碳发展
- C. 全面取消高考
- D. 扩大高水平对外开放
**题型**：多选
**答案**：ABD
**解析**：2026 年政府工作报告明确上述三项；C 项与事实不符，为干扰项。
**知识点**：政府工作报告
**难度**：2

## Q3
**题干**：《民法典》自 2021 年 1 月 1 日起施行。
- A. 正确
- B. 错误
**题型**：判断
**答案**：A
**解析**：《中华人民共和国民法典》确实于 2021 年 1 月 1 日施行，同时废止原有单行法。
**知识点**：民法典
**难度**：1
)QUIZ";

// 内置模考样例卷（UTF-8，写入 quiz/模考/ 供首次试用；mode=exam + duration）
static const char* kSampleExamUtf8 = R"QUIZ(---
title: 2026-08-15 行测模考（样例）
date: 2026-08-15
category: 模考
mode: exam
duration: 30
source: 内置样例
model: none
---

# 2026-08-15 行测模考（样例）

> 本卷为内置模考样例，时长 30 分钟；倒计时归零将自动收卷并出报告。

## Q1
**题干**：2026 年中央一号文件继续把以下哪项作为主题？
- A. 推进乡村全面振兴
- B. 扩大高水平对外开放
- C. 深化科技体制改革
- D. 完善收入分配制度
**题型**：单选
**答案**：A
**解析**：2026 年中央一号文件继续把推进乡村全面振兴作为主题，并把粮食安全摆在突出位置。
**知识点**：中央一号文件
**难度**：1

## Q2
**题干**：下列属于新质生产力典型代表的有？（多选）
- A. 商业航天
- B. 低空经济
- C. 传统煤炭采掘
- D. 量子科技
**题型**：多选
**答案**：ABD
**解析**：商业航天、低空经济、量子科技均属于新质生产力范畴；传统煤炭采掘属于传统生产力。
**知识点**：新质生产力
**难度**：2

## Q3
**题干**：《中华人民共和国公务员法》规定，公务员录用考试采取笔试和面试的方式进行。
- A. 正确
- B. 错误
**题型**：判断
**答案**：A
**解析**：公务员法明确录用采取公开考试、严格考察、平等竞争、择优录取，考试含笔试与面试。
**知识点**：公务员法
**难度**：1

## Q4
**题干**：2026 年政府工作报告提出的经济增长预期目标约为？
- A. 4% 左右
- B. 5% 左右
- C. 6% 左右
- D. 7% 左右
**题型**：单选
**答案**：B
**解析**：2026 年政府工作报告将国内生产总值增长预期目标设在 5% 左右。
**知识点**：政府工作报告
**难度**：2

## Q5
**题干**：以下哪项是行政强制措施的种类？（多选）
- A. 查封场所、设施
- B. 扣押财物
- C. 行政拘留
- D. 冻结存款、汇款
**题型**：多选
**答案**：ABD
**解析**：行政强制措施包括限制人身自由、查封扣押、冻结存款汇款等；行政拘留属行政处罚。
**知识点**：行政强制法
**难度**：3

## Q6
**题干**：数字媒体艺术属于艺术学门类下的专业类。
- A. 正确
- B. 错误
**题型**：判断
**答案**：A
**解析**：数字媒体艺术归于艺术学门类设计学类，是艺术学下设专业。
**知识点**：学科分类
**难度**：1
)QUIZ";

void QuizView::OnEnter()
{
    View::OnEnter();
    // Phase 4：绑定当前账户根目录，载入调度设置并应用到调度器；刷新薄弱知识点
    quiz::QuizStore::Instance().SetRoot(AccountStore::Instance().CurrentRoot());
    m_set = quiz::QuizStore::Instance().LoadSettings();
    ApplySettings();
    m_weakPoints = quiz::QuizStore::Instance().TopWeakPoints(8);
    ScanQuizzes();
}

// 本地今日日期键 YYYY-MM-DD
static std::wstring TodayKey()
{
    time_t t = time(nullptr);
    struct tm tm; localtime_s(&tm, &t);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

void QuizView::Toast(const std::wstring& m)
{
    m_toastMsg = m;
    m_toast = true;
    m_toastT = 0.0f;
}

// ---------------- 目录扫描 ----------------
void QuizView::EnumMd(const std::wstring& dir, const std::wstring& cat,
                      std::vector<QuizMeta>& out)
{
    CreateDirectoryW(dir.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.md").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        QuizMeta m;
        m.path = dir + fd.cFileName;
        m.category = cat;
        out.push_back(std::move(m));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void QuizView::SeedSampleIfEmpty()
{
    if (AccountStore::Instance().IsGuest()) return;   // 访客不写盘
    std::wstring root = AccountStore::Instance().CurrentRoot() + L"quiz\\";
    CreateDirectoryW(root.c_str(), nullptr);

    // 练习样例（时政）
    std::wstring dir = root + L"时政\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"2026-08-15.md";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(path, std::ios::binary);
        if (f) f << kSampleQuizUtf8;
    }

    // 模考样例（模考，含 duration）
    std::wstring edir = root + L"模考\\";
    CreateDirectoryW(edir.c_str(), nullptr);
    std::wstring epath = edir + L"2026-08-15-模考样例.md";
    if (GetFileAttributesW(epath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream ef(epath, std::ios::binary);
        if (ef) ef << kSampleExamUtf8;
    }
}

void QuizView::ScanQuizzes()
{
    m_list.clear();
    std::wstring root = AccountStore::Instance().CurrentRoot() + L"quiz\\";
    CreateDirectoryW(root.c_str(), nullptr);

    // 直接放在 quiz/ 下的 .md
    EnumMd(root, L"未分类", m_list);
    // 一层类别文件夹：quiz/<类别>/*.md
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                EnumMd(root + name + L"\\", name, m_list);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // 解析元信息
    for (auto& m : m_list) {
        quiz::QuizDoc d;
        if (quiz::LoadQuiz(m.path, d)) {
            m.title = d.title.empty() ? d.meta[L"title"] : d.title;
            if (m.title.empty()) {
                size_t p = m.path.find_last_of(L"\\/");
                m.title = m.path.substr(p + 1);
            }
            m.date = d.meta[L"date"];
            if (m.category == L"未分类" && !d.meta[L"category"].empty())
                m.category = d.meta[L"category"];
            m.mode = d.meta[L"mode"];                       // quiz / exam
            m.durationMin = ParseDurationMin(d.meta[L"duration"]);
            m.count = (int)d.questions.size();
        }
    }

    if (m_list.empty()) SeedSampleIfEmpty();
}

void QuizView::OpenDoc(const std::wstring& path)
{
    quiz::QuizDoc d;
    if (!quiz::LoadQuiz(path, d) || d.questions.empty()) {
        Toast(L"试卷解析失败或为空");
        return;
    }
    m_doc = std::move(d);
    m_sel.assign(m_doc.questions.size(), {});
    m_submitted = false;
    m_correct.clear();
    m_correctCount = 0;
    m_qScroll = 0.0f;

    // Phase 3：模考模式识别（frontmatter mode=exam + duration）
    m_exam = (m_doc.meta[L"mode"] == L"exam");
    m_examTimedOut = false;
    m_examElapsed = 0.0f;
    if (m_exam) {
        int dur = ParseDurationMin(m_doc.meta[L"duration"]);
        if (dur <= 0) dur = 60;                 // 缺省 60 分钟兜底
        m_examTotalSec = (float)dur * 60.0f;
    } else {
        m_examTotalSec = 0.0f;
    }
    m_report = quiz::QuizReport{};

    m_open = true;
    m_modalAnim = 0.0f;
    // Phase 4-2：模考进行中抑制自动出题（考试模式零打扰）
    if (m_exam) QuizScheduler::Instance().SetQuiet(true);
}

void QuizView::CloseDoc()
{
    m_open = false;
    m_doc = quiz::QuizDoc{};
    m_sel.clear();
    m_qItems.clear();
    m_exam = false;
    m_examTotalSec = 0.0f;
    m_examElapsed = 0.0f;
    m_examTimedOut = false;
    m_report = quiz::QuizReport{};
    QuizScheduler::Instance().SetQuiet(false);   // 退出模考恢复自动出题
}

void QuizView::Submit()
{
    m_correct.assign(m_doc.questions.size(), false);
    m_correctCount = 0;
    for (size_t i = 0; i < m_doc.questions.size(); ++i) {
        bool ok = quiz::IsCorrect(m_doc.questions[i], m_sel[i]);
        m_correct[i] = ok;
        if (ok) ++m_correctCount;
    }
    m_submitted = true;
    // Phase 3：模考报告（纯逻辑，无 Win32）
    m_report = quiz::ComputeQuizReport(m_doc, m_sel,
                                       (int)m_examTotalSec,
                                       (int)m_examElapsed,
                                       m_examTimedOut);
    QuizScheduler::Instance().SetQuiet(false);   // 交卷后恢复自动出题

    // Phase 4-1：成绩落盘 + 薄弱知识点记录
    if (!AccountStore::Instance().IsGuest()) {
        quiz::QuizAttempt a;
        a.ts = (long long)time(nullptr);
        a.dateKey = TodayKey();
        a.title = m_doc.title;
        a.category = m_doc.meta[L"category"].empty() ? (m_exam ? L"模考" : L"未分类") : m_doc.meta[L"category"];
        a.mode = m_exam ? L"exam" : L"quiz";
        a.total = (int)m_doc.questions.size();
        a.correct = m_correctCount;
        a.wrong = m_report.wrong;
        a.unanswered = m_report.unanswered;
        a.accuracy = m_report.accuracy;
        a.plannedSec = m_report.plannedSec;
        a.usedSec = m_report.usedSec;
        a.timeout = m_report.timeout;
        for (size_t i = 0; i < m_doc.questions.size(); ++i)
            if (i < m_report.correctByQ.size() && !m_report.correctByQ[i] && !m_doc.questions[i].point.empty())
                a.weakPoints.push_back(m_doc.questions[i].point);
        quiz::QuizStore::Instance().RecordAttempt(a);
        m_weakPoints = quiz::QuizStore::Instance().TopWeakPoints(8);
    }
    Toast(L"提交完成：正确 " + std::to_wstring(m_correctCount) + L" / "
          + std::to_wstring((int)m_doc.questions.size()));
}

void QuizView::ResetAnswers()
{
    m_sel.assign(m_doc.questions.size(), {});
    m_submitted = false;
    m_correct.clear();
    m_correctCount = 0;
    m_qScroll = 0.0f;
    // 模考：重置计时（重新模考），仍处模考中保持安静
    m_examElapsed = 0.0f;
    m_examTimedOut = false;
    m_report = quiz::QuizReport{};
    if (m_exam) QuizScheduler::Instance().SetQuiet(true);
}

// ---------------- 列表布局（屏幕坐标） ----------------
void QuizView::ComputeListRects()
{
    m_listRects.clear();
    m_listPaths.clear();
    float x0 = m_area.left + 28.0f;
    float w = (m_area.right - m_area.left) - 56.0f;
    float y = m_area.top + 168.0f;
    float cardH = 84.0f;
    for (size_t i = 0; i < m_list.size(); ++i) {
        m_listRects.push_back({ x0, y, x0 + w, y + cardH });
        m_listPaths.push_back(m_list[i].path);
        y += cardH + 12.0f;
    }
    // 「立即生成今日时政」按钮（列表头右上）
    float bw = 168.0f, bh = 34.0f;
    m_genBtn = { m_area.right - 28.0f - bw, m_area.top + 24.0f,
                 m_area.right - 28.0f, m_area.top + 24.0f + bh };
    // 「进入全屏考场模考」通栏按钮（标题/状态之下、列表之上）
    float ebw = (m_area.right - m_area.left) - 56.0f, ebh = 44.0f;
    m_examBtn = { m_area.left + 28.0f, m_area.top + 108.0f,
                  m_area.right - 28.0f, m_area.top + 108.0f + ebh };

    // 列表底部「薄弱知识点」卡（内容坐标，随滚动平移）
    if (!m_weakPoints.empty()) {
        int n = (int)m_weakPoints.size(); if (n > 8) n = 8;
        float weakH = 44.0f + (float)n * 26.0f + 12.0f;
        m_weakRect = { x0, y, x0 + w, y + weakH };
        y += weakH + 12.0f;
    } else {
        m_weakRect = {};
    }
    SetContentHeight(y - m_area.top);
}

// ---------------- 弹层布局（无需 cv） ----------------
void QuizView::LayoutModalRects()
{
    float aw = m_area.right - m_area.left;
    float ah = m_area.bottom - m_area.top;
    float w = aw - 48.0f; if (w > 1000.0f) w = 1000.0f;
    float h = ah - 48.0f; if (h > 680.0f) h = 680.0f;
    float x = m_area.left + (aw - w) * 0.5f;
    float y = m_area.top + (ah - h) * 0.5f;
    m_card = { x, y, x + w, y + h };

    float hudH = m_exam ? 44.0f : 0.0f;            // 模考倒计时 HUD 高度
    float bodyTop = m_card.top + 60.0f + hudH;
    float bodyBot = m_card.bottom - 16.0f;
    if (m_exam)
        m_hud = { m_card.left + 18.0f, m_card.top + 60.0f,
                  m_card.right - 18.0f, m_card.top + 60.0f + hudH - 6.0f };
    else
        m_hud = {};
    float pad = 18.0f;
    float rightW = 280.0f;
    float gap = 16.0f;
    m_ansPanel = { m_card.right - pad - rightW, bodyTop, m_card.right - pad, bodyBot };
    m_qBody = { m_card.left + pad, bodyTop, m_ansPanel.left - gap, bodyBot };
    m_closeBtn = { m_card.right - pad - 30.0f, m_card.top + 16.0f, m_card.right - pad, m_card.top + 44.0f };

    m_ansSquares.clear();
    int n = (int)m_doc.questions.size();
    int cols = 5; float sq = 34.0f, g = 8.0f;
    float gx = m_ansPanel.left + 14.0f;
    float gy = m_ansPanel.top + 52.0f;
    for (int i = 0; i < n; ++i) {
        int cx = i % cols, cy = i / cols;
        m_ansSquares.push_back({ gx + cx * (sq + g), gy + cy * (sq + g),
                                 gx + cx * (sq + g) + sq, gy + cy * (sq + g) + sq });
    }
    m_submitBtn = { m_ansPanel.left + 12.0f, m_ansPanel.bottom - 50.0f,
                    m_ansPanel.right - 12.0f, m_ansPanel.bottom - 14.0f };
}

// ---------------- 题目块布局（需 cv 测量换行） ----------------
void QuizView::LayoutQuestions(Canvas& cv)
{
    m_qItems.clear();
    float qw = m_qBody.right - m_qBody.left - 24.0f;
    float y = 0.0f;
    TextStyle stem; stem.size = 14.0f; stem.role = FontRole::Sans; stem.vAlign = VAlign::Top;
    TextStyle opt;  opt.size = 13.5f; opt.role = FontRole::Sans; opt.vAlign = VAlign::Middle;

    for (size_t qi = 0; qi < m_doc.questions.size(); ++qi) {
        const auto& q = m_doc.questions[qi];
        QItem it; it.q = (int)qi; it.top = y;

        float headerH = 24.0f;
        float stemH = cv.MeasureHeight(q.stem, stem, qw);
        it.stemH = stemH;

        float oy = y + headerH + stemH + 8.0f;
        for (size_t oi = 0; oi < q.options.size(); ++oi) {
            std::wstring txt = q.options[oi].key + L". " + q.options[oi].text;
            float oh = cv.MeasureHeight(txt, opt, qw);
            float rowH = (std::max)(30.0f, oh + 12.0f);
            it.optRects.push_back({ m_qBody.left + 12.0f, oy, m_qBody.right - 12.0f, oy + rowH });
            oy += rowH + 8.0f;
        }
        it.bottom = oy;

        if (m_submitted && !q.explain.empty()) {
            std::wstring exp = L"解析：" + q.explain;
            it.explainH = cv.MeasureHeight(exp, opt, qw) + 16.0f;
            it.bottom += it.explainH + 12.0f;
        } else {
            it.explainH = 0.0f;
        }
        y = it.bottom + 22.0f;
        m_qItems.push_back(it);
    }
    m_qContentH = m_qItems.empty() ? 0.0f : m_qItems.back().bottom;

    float wh = m_qBody.bottom - m_qBody.top;
    float maxS = (std::max)(0.0f, m_qContentH - wh);
    m_qScroll = (std::min)(m_qScroll, maxS);
}

// ---------------- 更新 ----------------
void QuizView::Update(float dt, const Input& in)
{
    if (m_toast) { m_toastT += dt; if (m_toastT > 2.4f) m_toast = false; }

    if (m_open) {
        // Phase 3：模考倒计时（未交卷时累积 dt；归零自动收卷判分）
        if (m_exam && !m_submitted) {
            m_examElapsed += dt;
            if (m_examElapsed >= m_examTotalSec) {
                m_examElapsed = m_examTotalSec;
                m_examTimedOut = true;
                Submit();                 // 自动收卷
                Toast(L"时间到，已自动收卷");
            }
        }
        LayoutModalRects();
        float mx = in.mouseX, my = in.mouseY;
        if (in.keyDown[VK_ESCAPE]) { CloseDoc(); return; }
        if (in.wheel != 0.0f) {
            float wh = m_qBody.bottom - m_qBody.top;
            float maxS = (std::max)(0.0f, m_qContentH - wh);
            m_qScroll = (std::max)(0.0f, (std::min)(maxS, m_qScroll + in.wheel * 48.0f));
            return;
        }
        if (in.clicked) {
            if (InRect(m_closeBtn, mx, my)) { CloseDoc(); return; }
            if (InRect(m_submitBtn, mx, my)) {
                if (!m_submitted) Submit(); else ResetAnswers();
                return;
            }
            // 答题卡方块 → 跳题
            for (size_t i = 0; i < m_ansSquares.size(); ++i) {
                if (InRect(m_ansSquares[i], mx, my) && i < m_doc.questions.size()) {
                    float target = (i < m_qItems.size()) ? m_qItems[i].top : 0.0f;
                    float wh = m_qBody.bottom - m_qBody.top;
                    float maxS = (std::max)(0.0f, m_qContentH - wh);
                    m_qScroll = (std::max)(0.0f, (std::min)(maxS, target));
                    return;
                }
            }
            // 点卡外关闭
            if (!InRect(m_card, mx, my)) { CloseDoc(); return; }
            // 选项点击（仅未提交）
            if (!m_submitted) {
                float cy = my - m_qBody.top + m_qScroll;   // 内容坐标
                for (size_t qi = 0; qi < m_qItems.size(); ++qi) {
                    const auto& it = m_qItems[qi];
                    if (cy < it.top || cy > it.bottom) continue;
                    const auto& q = m_doc.questions[it.q];
                    bool multi = (q.type == L"多选");
                    for (size_t oi = 0; oi < it.optRects.size(); ++oi) {
                        const auto& r = it.optRects[oi];
                        if (cy >= r.top && cy <= r.bottom) {
                            std::wstring key = q.options[oi].key;
                            auto& sel = m_sel[it.q];
                            auto f = std::find(sel.begin(), sel.end(), key);
                            if (multi) {
                                if (f == sel.end()) sel.push_back(key); else sel.erase(f);
                            } else {
                                sel.clear(); sel.push_back(key);
                            }
                            return;
                        }
                    }
                }
            }
            return;
        }
        return;   // 弹层内不滚动列表
    }

    // ---- 列表模式 ----
    ComputeListRects();

    // 生成完成后自动重扫列表（把新卷纳入）
    const auto& last = QuizScheduler::Instance().Last();
    if (last.ts != m_lastSeenGenTs) {
        m_lastSeenGenTs = last.ts;
        if (last.ok) ScanQuizzes();
    }

    View::Update(dt, in);
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    if (in.clicked) {
        if (InRect(m_examBtn, in.mouseX, in.mouseY)) {
            ExamMode::Instance().Start(120);   // 进入全屏考场模考（120 分钟行测）
            return;
        }
        if (InRect(m_genBtn, in.mouseX, in.mouseY)) {
            QuizScheduler::Instance().KickNow();
            Toast(L"已开始生成，请稍候…");
            return;
        }
        for (size_t i = 0; i < m_listRects.size(); ++i) {
            if (InRect(m_listRects[i], shifted.mouseX, shifted.mouseY)) {
                OpenDoc(m_list[i].path);
                return;
            }
        }
    }
}

// ---------------- 绘制：列表 ----------------
void QuizView::PaintList(Canvas& cv)
{
    const auto& pal = cv.Pal();
    ComputeListRects();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -ScrollY()));

    TextStyle ht; ht.size = 22.0f; ht.role = FontRole::Serif; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"练考 · 时政 / 模考", { m_area.left + 28.0f, m_area.top + 24.0f,
            m_area.right - 28.0f, m_area.top + 60.0f }, ht, pal.ink900);
    TextStyle sub; sub.size = 12.0f; sub.role = FontRole::Sans;
    cv.Text(L"扫描 accounts/<账户>/quiz/<类别>/*.md（详见《题目MD规范》），点开即练；提交后显示解析",
            { m_area.left + 28.0f, m_area.top + 62.0f, m_genBtn.left - 14.0f, m_area.top + 84.0f }, sub, pal.ink500);

    // ===== 顶部右侧：立即生成按钮 + 状态 =====
    bool gen = QuizScheduler::Instance().IsGenerating();
    cv.FillRoundRect(m_genBtn, 8.0f, gen ? WithAlpha(pal.seal, 0.55f) : pal.seal);
    cv.StrokeRoundRect(m_genBtn, 8.0f, pal.seal, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans; bt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle; bt.letterSpacing = 0.5f;
    cv.Text(gen ? L"生成中…" : L"立即生成今日时政", m_genBtn, bt, pal.paperHi);

    const auto& last = QuizScheduler::Instance().Last();
    if (last.ts > 0) {
        TextStyle st; st.size = 11.5f; st.role = FontRole::Sans; st.vAlign = VAlign::Middle;
        st.hAlign = HAlign::Right;
        D2D1_COLOR_F scol = last.ok ? WithAlpha(pal.jade, 0.9f) : WithAlpha(pal.vermilion, 0.9f);
        cv.Text(last.msg, { m_area.left + 28.0f, m_area.top + 88.0f, m_genBtn.left - 14.0f, m_area.top + 100.0f },
                st, scol);
    }

    // ===== 通栏 CTA：进入全屏考场模考（120 分钟，全屏零打扰）=====
    {
        D2D1_RECT_F eb = m_examBtn;
        cv.FillRoundRect(eb, 10.0f, pal.seal);
        cv.StrokeRoundRect(eb, 10.0f, pal.seal, shape::kHair);
        TextStyle et; et.size = 14.5f; et.role = FontRole::Sans; et.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        et.vAlign = VAlign::Middle; et.hAlign = HAlign::Left;
        cv.Text(L"进入全屏考场模考", { eb.left + 18.0f, eb.top, eb.right - 150.0f, eb.bottom }, et, pal.paperHi);
        TextStyle eh; eh.size = 12.0f; eh.role = FontRole::Sans; eh.vAlign = VAlign::Middle; eh.hAlign = HAlign::Right;
        cv.Text(L"120 分钟 · 全屏零打扰",
                { eb.right - 210.0f, eb.top, eb.right - 18.0f, eb.bottom }, eh, WithAlpha(pal.paperHi, 0.88f));
    }

    if (m_list.empty()) {
        D2D1_RECT_F hr = { m_area.left + 28.0f, m_area.top + 168.0f, m_area.right - 28.0f, m_area.top + 268.0f };
        cv.PaperCard(hr, 2.0f);
        cv.StrokeRoundRect(hr, 2.0f, pal.rule, shape::kHair);
        TextStyle t1; t1.size = 15.0f; t1.role = FontRole::Sans; t1.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(L"尚未发现试卷", { hr.left + 24.0f, hr.top + 18.0f, hr.right - 24.0f, hr.top + 44.0f }, t1, pal.ink900);
        TextStyle t2; t2.size = 12.5f; t2.role = FontRole::Sans; t2.vAlign = VAlign::Top;
        cv.Text(L"把符合《题目MD规范》的 .md 放进 accounts/<账户>/quiz/时政/ 等文件夹即可出现在此；首次已自动放置一份样例卷供试用。",
                { hr.left + 24.0f, hr.top + 52.0f, hr.right - 24.0f, hr.top + 92.0f }, t2, pal.ink500);
    } else {
        for (size_t i = 0; i < m_list.size(); ++i) {
            const auto& r = m_listRects[i];
            const auto& m = m_list[i];
            bool exam = (m.mode == L"exam");
            cv.PaperCard(r, 2.0f);
            cv.StrokeRoundRect(r, 2.0f, pal.rule, shape::kHair);
            cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom },
                        WithAlpha(exam ? pal.seal : pal.jade, 0.85f));

            TextStyle t1; t1.size = 15.0f; t1.role = FontRole::Sans; t1.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            cv.Text(m.title, { r.left + 24.0f, r.top + 14.0f, r.right - 110.0f, r.top + 40.0f }, t1, pal.ink900);
            TextStyle t2; t2.size = 12.0f; t2.role = FontRole::Sans;
            std::wstring meta = exam
                ? (L"模考 · " + std::to_wstring(m.durationMin) + L" 分钟  ·  " + m.date + L"  ·  " + std::to_wstring(m.count) + L" 题")
                : (m.category + L"  ·  " + m.date + L"  ·  " + std::to_wstring(m.count) + L" 题");
            cv.Text(meta, { r.left + 24.0f, r.top + 46.0f, r.right - 24.0f, r.top + 70.0f }, t2, pal.ink500);
            TextStyle go; go.size = 12.5f; go.role = FontRole::Sans; go.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            go.hAlign = HAlign::Right;
            cv.Text(exam ? L"进入模考 ›" : L"打开 ›",
                    { r.right - 100.0f, r.top + 14.0f, r.right - 20.0f, r.top + 40.0f }, go, exam ? pal.seal : pal.ink700);
        }
    }

    if (!m_weakPoints.empty()) PaintWeakCard(cv, m_weakRect);

    cv.PopTransform();
    cv.PopClip();
}

// ---------------- Phase 4：薄弱知识点卡 ----------------
void QuizView::PaintWeakCard(Canvas& cv, const D2D1_RECT_F& r)
{
    const auto& pal = cv.Pal();
    cv.PaperCard(r, 2.0f);
    cv.StrokeRoundRect(r, 2.0f, pal.rule, shape::kHair);
    cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom }, WithAlpha(pal.brass, 0.85f));

    TextStyle t1; t1.size = 15.0f; t1.role = FontRole::Sans; t1.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(L"薄弱知识点（按答错次数）", { r.left + 24.0f, r.top + 14.0f, r.right - 24.0f, r.top + 40.0f }, t1, pal.ink900);

    TextStyle t2; t2.size = 12.5f; t2.role = FontRole::Sans;
    float yy = r.top + 46.0f;
    int n = (int)m_weakPoints.size(); if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        const auto& wp = m_weakPoints[i];
        D2D1_COLOR_F dot = pal.brass;
        if (wp.second >= 3) dot = pal.vermilion;
        else if (wp.second >= 2) dot = pal.seal;
        cv.FillRect({ r.left + 26.0f, yy + 6.0f, r.left + 32.0f, yy + 12.0f }, dot);
        cv.Text(wp.first, { r.left + 44.0f, yy, r.right - 90.0f, yy + 22.0f }, t2, pal.ink900);
        TextStyle cc; cc.size = 12.0f; cc.role = FontRole::Sans; cc.hAlign = HAlign::Right; cc.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(std::to_wstring(wp.second) + L" 次", { r.right - 86.0f, yy, r.right - 24.0f, yy + 22.0f }, cc, dot);
        yy += 26.0f;
    }
}

// ---------------- Phase 4：调度配置推送（设置 UI 已迁至 SettingsView）----------------
void QuizView::ApplySettings()
{
    quiz::QuizStore::Instance().SaveSettings(m_set);
    QuizScheduler::Instance().SetConfig(m_set.enabled, m_set.hour, m_set.minute,
                                        m_set.category, m_set.qcount, m_set.rss);
}

void QuizView::PaintModal(Canvas& cv)
{
    const auto& pal = cv.Pal();
    m_modalAnim += (1.0f - m_modalAnim) * 0.3f;
    if (m_modalAnim > 0.996f) m_modalAnim = 1.0f;
    float a = m_modalAnim;

    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.55f * a));
    cv.PushOpacity(a);

    cv.PaperCard(m_card, 0.5f, shape::kEdge);
    cv.DoubleFrame(m_card, WithAlpha(pal.seal, 0.8f));

    TextStyle tt; tt.size = 20.0f; tt.role = FontRole::Serif; tt.weight = DWRITE_FONT_WEIGHT_BOLD; tt.letterSpacing = 1.2f;
    cv.Text(m_doc.title.empty() ? L"试卷" : m_doc.title,
            { m_card.left + 24.0f, m_card.top + 14.0f, m_card.right - 80.0f, m_card.top + 48.0f }, tt, pal.ink900);
    cv.FillRoundRect(m_closeBtn, 6.0f, WithAlpha(pal.rule, 0.12f));
    cv.StrokeRoundRect(m_closeBtn, 6.0f, pal.rule, shape::kHair);
    TextStyle ct; ct.size = 13.0f; ct.role = FontRole::Sans; ct.hAlign = HAlign::Center; ct.vAlign = VAlign::Middle;
    cv.Text(L"关闭", m_closeBtn, ct, pal.ink700);

    // Phase 3：模考倒计时 HUD（顶部，标题与题区之间）
    if (m_exam) PaintExamHud(cv);

    // ===== 左侧题目 =====
    cv.PushClip(m_qBody);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, m_qBody.top - m_qScroll));
    {
        float qw = m_qBody.right - m_qBody.left - 24.0f;
        for (size_t qi = 0; qi < m_qItems.size(); ++qi) {
            const auto& it = m_qItems[qi];
            const auto& q = m_doc.questions[it.q];
            float x = m_qBody.left + 12.0f;

            TextStyle hs; hs.size = 12.5f; hs.role = FontRole::Sans; hs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            std::wstring hdr = L"Q" + std::to_wstring(it.q + 1) + L"  ·  " + q.type
                             + L"  ·  难度 " + std::to_wstring(q.difficulty);
            cv.Text(hdr, { x, it.top, x + qw, it.top + 22.0f }, hs, pal.ink500);

            TextStyle ss; ss.size = 14.0f; ss.role = FontRole::Sans; ss.vAlign = VAlign::Top;
            cv.Text(q.stem, { x, it.top + 24.0f, x + qw, it.top + 24.0f + it.stemH }, ss, pal.ink900);

            float oy = it.top + 24.0f + it.stemH + 8.0f;
            for (size_t oi = 0; oi < q.options.size(); ++oi) {
                const auto& r = it.optRects[oi];
                bool selected = std::find(m_sel[it.q].begin(), m_sel[it.q].end(), q.options[oi].key) != m_sel[it.q].end();
                bool isAnswer = std::find(q.answer.begin(), q.answer.end(), q.options[oi].key) != q.answer.end();
                D2D1_COLOR_F fill = WithAlpha(pal.rule, 0.08f);
                D2D1_COLOR_F border = pal.rule;
                D2D1_COLOR_F fg = pal.ink700;
                if (m_submitted) {
                    if (isAnswer)        { fill = WithAlpha(pal.jade, 0.16f);       border = pal.jade;       fg = pal.ink900; }
                    else if (selected)   { fill = WithAlpha(pal.vermilion, 0.16f);  border = pal.vermilion;  fg = pal.ink900; }
                } else if (selected) {
                    fill = WithAlpha(pal.seal, 0.14f); border = pal.seal; fg = pal.ink900;
                }
                cv.FillRoundRect(r, 6.0f, fill);
                cv.StrokeRoundRect(r, 6.0f, border, shape::kHair);
                std::wstring txt = q.options[oi].key + L". " + q.options[oi].text;
                TextStyle os; os.size = 13.5f; os.role = FontRole::Sans; os.vAlign = VAlign::Middle;
                cv.Text(txt, { r.left + 10.0f, r.top, r.right - 10.0f, r.bottom }, os, fg);
                oy = r.bottom + 8.0f;
            }
            if (m_submitted && !q.explain.empty()) {
                D2D1_RECT_F eb = { x, oy + 2.0f, x + qw, oy + 2.0f + it.explainH };
                cv.FillRoundRect(eb, 6.0f, WithAlpha(pal.jade, 0.08f));
                cv.StrokeRoundRect(eb, 6.0f, WithAlpha(pal.jade, 0.4f), shape::kHair);
                TextStyle es; es.size = 12.5f; es.role = FontRole::Sans; es.vAlign = VAlign::Top;
                cv.Text(L"解析：" + q.explain, { eb.left + 10.0f, eb.top + 8.0f, eb.right - 10.0f, eb.bottom - 8.0f }, es, pal.ink700);
            }
        }
    }
    cv.PopTransform();
    cv.PopClip();

    // 左侧滚动条指示
    float wh = m_qBody.bottom - m_qBody.top;
    if (m_qContentH > wh + 1.0f) {
        float bx = m_qBody.right + 9.0f;
        cv.FillRoundRect({ bx - 2.0f, m_qBody.top, bx + 2.0f, m_qBody.bottom }, 2.0f, WithAlpha(pal.ink300, 0.18f));
        float th = (std::max)(28.0f, wh * wh / m_qContentH);
        float ty = m_qBody.top + (m_qBody.bottom - m_qBody.top - th) * (m_qScroll / (m_qContentH - wh));
        cv.FillRoundRect({ bx - 2.0f, ty, bx + 2.0f, ty + th }, 2.0f, WithAlpha(pal.seal, 0.6f));
    }

    // ===== 右侧答题卡 =====
    cv.PaperCard(m_ansPanel, 0.5f);
    cv.StrokeRoundRect(m_ansPanel, 0.5f, pal.rule, shape::kHair);
    TextStyle at; at.size = 13.0f; at.role = FontRole::Sans; at.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; at.letterSpacing = 1.0f;
    cv.Text(L"答题卡", { m_ansPanel.left + 14.0f, m_ansPanel.top + 16.0f, m_ansPanel.right - 14.0f, m_ansPanel.top + 40.0f }, at, pal.ink900);

    for (size_t i = 0; i < m_ansSquares.size(); ++i) {
        const auto& r = m_ansSquares[i];
        bool answered = !m_sel[i].empty();
        D2D1_COLOR_F fill, border, fg;
        if (m_submitted) {
            if (m_correct[i]) { fill = WithAlpha(pal.jade, 0.85f); border = pal.jade; fg = pal.paperHi; }
            else              { fill = WithAlpha(pal.vermilion, 0.85f); border = pal.vermilion; fg = pal.paperHi; }
        } else if (answered) { fill = WithAlpha(pal.seal, 0.18f); border = pal.seal; fg = pal.seal; }
        else                 { fill = WithAlpha(pal.ink300, 0.16f); border = pal.rule; fg = pal.ink500; }
        cv.FillRoundRect(r, 6.0f, fill);
        cv.StrokeRoundRect(r, 6.0f, border, shape::kHair);
        TextStyle sqs; sqs.size = 12.0f; sqs.role = FontRole::Sans; sqs.hAlign = HAlign::Center; sqs.vAlign = VAlign::Middle;
        cv.Text(std::to_wstring(i + 1), r, sqs, fg);
    }

    int rows = (int)m_ansSquares.size() / 5 + ((int)m_ansSquares.size() % 5 ? 1 : 0);
    float sy = m_ansPanel.top + 52.0f + rows * (34.0f + 8.0f) + 10.0f;
    TextStyle st; st.size = 12.5f; st.role = FontRole::Sans; st.vAlign = VAlign::Top;
    if (m_exam && m_submitted) {
        PaintExamReport(cv);     // Phase 3：模考报告
    } else if (m_submitted) {
        cv.Text(L"得分 " + std::to_wstring(m_correctCount) + L" / " + std::to_wstring((int)m_doc.questions.size()),
                { m_ansPanel.left + 14.0f, sy, m_ansPanel.right - 14.0f, sy + 24.0f }, st, pal.ink900);
        int unans = 0; for (auto& v : m_sel) if (v.empty()) ++unans;
        cv.Text(L"未答 " + std::to_wstring(unans) + L" 题",
                { m_ansPanel.left + 14.0f, sy + 28.0f, m_ansPanel.right - 14.0f, sy + 50.0f }, st, pal.ink500);
    } else {
        int unans = 0; for (auto& v : m_sel) if (v.empty()) ++unans;
        cv.Text(L"已答 " + std::to_wstring((int)m_sel.size() - unans) + L" / " + std::to_wstring((int)m_doc.questions.size()),
                { m_ansPanel.left + 14.0f, sy, m_ansPanel.right - 14.0f, sy + 24.0f }, st, pal.ink700);
    }

    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, D2D1_COLOR_F col, D2D1_COLOR_F tx) {
        cv.FillRoundRect(r, 8.0f, col);
        cv.StrokeRoundRect(r, 8.0f, col, shape::kHair);
        TextStyle bs; bs.size = 13.0f; bs.role = FontRole::Sans; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(label, r, bs, tx);
    };
    if (m_submitted) Btn(m_submitBtn, m_exam ? L"重新模考" : L"重新答题", pal.paperLo, pal.ink700);
    else             Btn(m_submitBtn, m_exam ? L"交卷" : L"提交", pal.seal, pal.paperHi);

    cv.PopOpacity();
}

// ---------------- Phase 3：模考倒计时 HUD ----------------
void QuizView::PaintExamHud(Canvas& cv)
{
    const auto& pal = cv.Pal();
    cv.FillRoundRect(m_hud, 8.0f, WithAlpha(pal.rule, 0.10f));

    float rem = (std::max)(0.0f, m_examTotalSec - m_examElapsed);
    int remSec = (int)rem;
    int pct = m_examTotalSec > 0 ? (int)(100.0f * rem / m_examTotalSec) : 0;
    D2D1_COLOR_F col = pal.jade;
    if (m_examTimedOut) col = pal.vermilion;
    else if (pct <= 10) col = pal.vermilion;
    else if (pct <= 30) col = pal.brass;

    float barX = m_hud.left + 12.0f;
    float barY = m_hud.top + 22.0f;
    float barW = m_hud.right - m_hud.left - 24.0f;
    float barH = 8.0f;
    cv.FillRoundRect({ barX, barY, barX + barW, barY + barH }, 4.0f, WithAlpha(pal.ink300, 0.25f));
    float used = (std::max)(0.0f, (std::min)(1.0f, m_examElapsed / (m_examTotalSec > 0 ? m_examTotalSec : 1.0f)));
    if (used > 0.0f)
        cv.FillRoundRect({ barX, barY, barX + barW * used, barY + barH }, 4.0f, col);

    TextStyle ls; ls.size = 12.5f; ls.role = FontRole::Sans; ls.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(L"模考倒计时", { m_hud.left + 12.0f, m_hud.top + 2.0f, m_hud.left + 120.0f, m_hud.top + 20.0f }, ls, pal.ink700);
    TextStyle rs; rs.size = 14.0f; rs.role = FontRole::Sans; rs.weight = DWRITE_FONT_WEIGHT_BOLD;
    rs.hAlign = HAlign::Right; rs.vAlign = VAlign::Middle;
    cv.Text(m_examTimedOut ? L"已收卷" : FmtMMSS(remSec),
            { m_hud.right - 130.0f, m_hud.top + 2.0f, m_hud.right - 12.0f, m_hud.top + 20.0f }, rs,
            m_examTimedOut ? pal.vermilion : (pct <= 10 ? pal.vermilion : pal.ink900));
}

// ---------------- Phase 3：模考报告面板（右侧） ----------------
void QuizView::PaintExamReport(Canvas& cv)
{
    const auto& pal = cv.Pal();
    int rows = (int)m_ansSquares.size() / 5 + ((int)m_ansSquares.size() % 5 ? 1 : 0);
    float sy = m_ansPanel.top + 52.0f + rows * (34.0f + 8.0f) + 14.0f;

    TextStyle ts; ts.size = 14.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"模考报告", { m_ansPanel.left + 14.0f, sy, m_ansPanel.right - 14.0f, sy + 26.0f }, ts, pal.ink900);

    float y = sy + 34.0f; const float lh = 24.0f;
    TextStyle fs; fs.size = 12.5f; fs.role = FontRole::Sans;
    auto line = [&](const std::wstring& k, const std::wstring& v, D2D1_COLOR_F c) {
        cv.Text(k, { m_ansPanel.left + 14.0f, y, m_ansPanel.left + 110.0f, y + 22.0f }, fs, pal.ink500);
        cv.Text(v, { m_ansPanel.left + 110.0f, y, m_ansPanel.right - 14.0f, y + 22.0f }, fs, c);
        y += lh;
    };
    line(L"得分", std::to_wstring(m_report.correct) + L" / " + std::to_wstring(m_report.total), pal.ink900);
    line(L"正确率", std::to_wstring((int)(m_report.accuracy + 0.5f)) + L"%",
         m_report.accuracy >= 60.0f ? pal.jade : pal.vermilion);
    line(L"用时", FmtMMSS(m_report.usedSec), pal.ink700);
    line(L"计划", FmtMMSS(m_report.plannedSec), pal.ink700);
    line(L"状态", m_report.timeout ? L"超时收卷" : L"完成",
         m_report.timeout ? pal.vermilion : pal.jade);
}

void QuizView::Paint(Canvas& cv)
{
    if (m_open) {
        PaintList(cv);          // 背后（被蒙层压暗）
        LayoutModalRects();
        LayoutQuestions(cv);
        PaintModal(cv);
        if (m_toast) DrawToast(cv);
        return;
    }
    PaintList(cv);
    if (m_toast) DrawToast(cv);
}

void QuizView::DrawToast(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float w = 280.0f, h = 40.0f;
    float x = m_area.right - w - 28.0f;
    float y = m_area.bottom - h - 28.0f;
    D2D1_RECT_F r{ x, y, x + w, y + h };
    cv.FillRoundRect(r, 8.0f, pal.seal);
    TextStyle ts; ts.size = 13.0f; ts.role = FontRole::Sans; ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(m_toastMsg, r, ts, pal.paperHi);
}

void QuizView::DebugForcePreview()
{
    if (m_list.empty()) { SeedSampleIfEmpty(); ScanQuizzes(); }
    if (!m_list.empty()) OpenDoc(m_list[0].path);
}

} // namespace lj
