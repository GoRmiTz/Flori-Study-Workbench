#include "views/CoverView.h"
#include "app/Data.h"
#include "app/AccountStore.h"

namespace lj {

void CoverView::GoEnter()
{
    // 封面进入：有会话直接进主应用；无会话先落登录页
    // （登录成功后由 LoginView 直接进主应用，不再折回封面）。
    if (AccountStore::Instance().HasSession())
        Go(L"home");
    else
        Go(L"login");
}

void CoverView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_parNear.Snap(0.0f); m_parMid.Snap(0.0f); m_parFar.Snap(0.0f);
    m_enterBtn.StartEnter(0.0f);
}

void CoverView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_pageH = area.bottom - area.top;
    SetContentHeight(m_pageH * 3.0f);

    // 第三幕中央的进入按钮（字距拉开的品牌名，随 Content 自动更名）
    const auto& C = Content::Get();
    std::wstring label;
    for (wchar_t ch : C.appName) { if (!label.empty()) label += L' '; label += ch; }
    float cx = area.left + (area.right - area.left) * 0.5f;
    float top = area.top + m_pageH * 2.0f + m_pageH * 0.56f;
    m_enterBtn.bounds = { cx - 104.0f, top, cx + 104.0f, top + 46.0f };
    m_enterBtn.label = label;
    m_enterBtn.primary = true;
    m_enterBtn.fontSize = 15.0f;
    m_enterBtn.onClick = [this] { GoEnter(); };

    m_widgets.clear();
    m_widgets.push_back(&m_enterBtn);
}

void CoverView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_rotorAngle += dt * 4.6f;             // 匀速旋转，交给 CSS 的那份差事

    float s = ScrollY();
    m_parNear.target = s; m_parMid.target = s; m_parFar.target = s;
    m_parNear.Update(dt); m_parMid.Update(dt); m_parFar.Update(dt);

    // 按钮位置随滚动移动，所以命中测试要用偏移后的坐标
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // 回车 / 空格直接进入
    if (in.keyDown[VK_RETURN] || in.keyDown[VK_SPACE]) GoEnter();
}

void CoverView::Paint(Canvas& cv)
{
    float s = ScrollY();
    PaintHero(cv, s);
    PaintIntro(cv, s);
    PaintEnter(cv, s);

    // ---- 滚动进度条：scrub 驱动，绝不加缓动 ----
    const auto& pal = cv.Pal();
    float maxS = (std::max)(MaxScroll(), 1.0f);
    float p = Clamp01(s / maxS);
    cv.FillRect({ m_area.left, m_area.top, m_area.left + (m_area.right - m_area.left) * p,
                  m_area.top + 2.0f }, pal.seal);
}

void CoverView::PaintHero(Canvas& cv, float scroll)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();

    float W = m_area.right - m_area.left;
    float cx = m_area.left + W * 0.5f;
    float baseY = m_area.top;
    if (scroll > m_pageH * 1.25f) return;   // 出屏即跳过

    // hero 内容：近景，随滚动上移并淡出（对应 y*0.32 + opacity）
    float heroShift = -m_parNear.value * 0.32f;
    float heroAlpha = Clamp01(1.0f - (scroll / m_pageH) * 1.15f);
    if (heroAlpha <= 0.004f) return;

    float cy = baseY + m_pageH * 0.46f;

    // ---- 测量环：远景，慢一档，只上移；旋转独立匀速 ----
    {
        float ry = cy - m_parFar.value * 0.12f;
        const wchar_t* words = L"DISCIPLINE · FOCUS · PERSISTENCE · GROWTH · ";
        size_t n = wcslen(words);
        float radius = (std::min)(190.0f, m_pageH * 0.26f);

        TextStyle rs;
        rs.role = FontRole::Mono;
        rs.size = 12.0f;
        rs.weight = DWRITE_FONT_WEIGHT_BOLD;
        rs.hAlign = HAlign::Center;
        rs.vAlign = VAlign::Middle;

        float appear = Clamp01((m_t - 0.15f) / 1.1f);
        cv.PushOpacity(heroAlpha * appear * 0.55f);
        for (size_t i = 0; i < n; ++i) {
            float ang = m_rotorAngle + (float)i / (float)n * 360.0f;
            cv.PushTransform(D2D1::Matrix3x2F::Rotation(ang, D2D1::Point2F(cx, ry)));
            wchar_t ch[2] = { words[i], 0 };
            D2D1_RECT_F box{ cx - 9.0f, ry - radius - 9.0f, cx + 9.0f, ry - radius + 9.0f };
            cv.Text(ch, box, rs, pal.ink500);
            cv.PopTransform();
        }
        cv.PopOpacity();

        // 内圈刻度环：制图仪的量角器
        cv.PushOpacity(heroAlpha * appear * 0.5f);
        cv.StrokeCircle(cx, ry, radius - 22.0f, WithAlpha(pal.rule, 0.9f), 1.0f);
        for (int i = 0; i < 72; ++i) {
            float a = (float)i / 72.0f * 6.283185f + m_rotorAngle * 0.0174533f * 0.3f;
            float r0 = radius - 22.0f;
            float r1 = r0 - ((i % 6 == 0) ? 7.0f : 3.0f);
            cv.Line(cx + std::cos(a) * r0, ry + std::sin(a) * r0,
                    cx + std::cos(a) * r1, ry + std::sin(a) * r1,
                    WithAlpha(pal.ink300, i % 6 == 0 ? 0.75f : 0.4f), 1.0f);
        }
        cv.PopOpacity();
    }

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, heroShift));
    cv.PushOpacity(heroAlpha);

    // ---- 主标题「芙洛理」逐字揭示 ----
    TextStyle big;
    big.role = FontRole::Serif;
    big.size = (std::min)(96.0f, m_pageH * 0.135f);
    big.weight = DWRITE_FONT_WEIGHT_BLACK;
    big.letterSpacing = big.size * 0.16f;
    float bw = cv.MeasureWidth(C.appName, big);
    cv.CharsReveal(C.appName, cx - bw * 0.5f, cy - big.size * 0.72f, big,
                   pal.ink900, m_t * 0.9f, 0.13f, 24.0f);

    // ---- 拉丁名 ----
    float la = Clamp01((m_t - 0.55f) / 0.6f);
    if (la > 0.0f) {
        TextStyle lat;
        lat.role = FontRole::Mono;
        lat.size = 13.0f;
        lat.weight = DWRITE_FONT_WEIGHT_BOLD;
        lat.letterSpacing = 9.0f;
        lat.hAlign = HAlign::Center;
        float e = ease::OutCubic(la);
        float lw2 = cv.MeasureWidth(C.appNameLatin, lat);
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 10.0f));
        cv.Text(C.appNameLatin, { cx - lw2 * 0.5f - 9.0f, cy + big.size * 0.42f,
                                  cx + lw2 * 0.5f + 9.0f, cy + big.size * 0.42f + 22.0f },
                lat, WithAlpha(pal.seal, e));
        cv.PopTransform();
    }

    // ---- 副标题 ----
    float sa = Clamp01((m_t - 0.85f) / 0.65f);
    if (sa > 0.0f) {
        float e = ease::OutCubic(sa);
        TextStyle sub;
        sub.role = FontRole::Sans;
        sub.size = 14.0f;
        sub.letterSpacing = 2.6f;
        sub.hAlign = HAlign::Center;
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
        cv.Text(C.subtitle, { cx - 320.0f, cy + big.size * 0.42f + 34.0f,
                              cx + 320.0f, cy + big.size * 0.42f + 60.0f },
                sub, WithAlpha(pal.ink500, e));
        cv.PopTransform();

        // 骑缝线
        float lw = 210.0f * e;
        cv.PerforationH(cx - lw, cx + lw, cy + big.size * 0.42f + 74.0f,
                        WithAlpha(pal.ruleStrong, e * 0.8f));
    }

    cv.PopOpacity();
    cv.PopTransform();

    // ---- 档案号（左上）与日期（右上）：中景视差 ----
    {
        float midShift = -m_parMid.value * 0.18f;
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, midShift));
        cv.PushOpacity(heroAlpha * Clamp01((m_t - 0.3f) / 0.7f));
        TextStyle tag;
        tag.role = FontRole::Mono; tag.size = 10.5f; tag.letterSpacing = 2.0f;
        cv.Text(C.dossierNo, { m_area.left + 46.0f, baseY + 40.0f, m_area.left + 400.0f, baseY + 58.0f },
                tag, pal.ink300);

        Date td = Today();
        std::wstring right = FormatDate(td) + L"  " + WeekdayCN(td);
        tag.hAlign = HAlign::Right;
        cv.Text(right, { m_area.right - 400.0f, baseY + 40.0f, m_area.right - 46.0f, baseY + 58.0f },
                tag, pal.ink300);
        cv.PopOpacity();
        cv.PopTransform();
    }

    // ---- 滚动提示：呼吸下箭头 ----
    {
        float ha = heroAlpha * Clamp01((m_t - 1.4f) / 0.8f);
        if (ha > 0.004f) {
            float bob = std::sin(m_t * 2.0f) * 3.0f;
            TextStyle hint;
            hint.role = FontRole::Mono; hint.size = 10.0f;
            hint.letterSpacing = 3.0f; hint.hAlign = HAlign::Center;
            float hy = baseY + m_pageH - 78.0f + bob;
            cv.Text(L"SCROLL", { cx - 120.0f, hy, cx + 120.0f, hy + 16.0f },
                    hint, WithAlpha(pal.ink300, ha));
            cv.Line(cx, hy + 22.0f, cx, hy + 40.0f, WithAlpha(pal.ink300, ha * 0.8f), 1.0f);
            cv.Line(cx - 4.0f, hy + 35.0f, cx, hy + 40.0f, WithAlpha(pal.ink300, ha * 0.8f), 1.0f);
            cv.Line(cx + 4.0f, hy + 35.0f, cx, hy + 40.0f, WithAlpha(pal.ink300, ha * 0.8f), 1.0f);
        }
    }
}

void CoverView::PaintIntro(Canvas& cv, float scroll)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float W = m_area.right - m_area.left;
    float cx = m_area.left + W * 0.5f;

    float sectionTop = m_area.top + m_pageH - scroll;
    if (sectionTop > m_area.bottom || sectionTop + m_pageH < m_area.top - 40.0f) return;

    // 逐条显影：元素进入视口 92% 即触发（与 forceRevealVisible 同阈值）
    float maxW = (std::min)(720.0f, W - 120.0f);
    float y = sectionTop + m_pageH * 0.30f;

    for (size_t i = 0; i < C.coverLines.size(); ++i) {
        TextStyle ls;
        ls.role = FontRole::Serif;
        ls.size = (i == 0) ? 30.0f : 19.0f;
        ls.weight = (i == 0) ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        ls.letterSpacing = 1.6f;
        ls.hAlign = HAlign::Center;

        float h = cv.MeasureHeight(C.coverLines[i], ls, maxW);
        float trigger = m_area.bottom - (m_area.bottom - m_area.top) * 0.08f;
        float appear = Clamp01((trigger - y) / 160.0f);
        float e = ease::OutCubic(appear);
        if (e > 0.004f) {
            cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 22.0f));
            cv.PushOpacity(e);
            cv.Text(C.coverLines[i], { cx - maxW * 0.5f, y, cx + maxW * 0.5f, y + h + 8.0f },
                    ls, (i == 0) ? pal.ink900 : pal.ink700);
            cv.PopOpacity();
            cv.PopTransform();
        }
        y += h + ((i == 0) ? 40.0f : 22.0f);
    }

    // 侧栏刻度：远景视差，制造纵深
    // 注意：far / near 是 windef.h 里的历史宏，变量名要避开
    float farOffset = -m_parFar.value * 0.05f;
    cv.PushOpacity(0.5f);
    for (int i = 0; i < 26; ++i) {
        float ly = sectionTop + 60.0f + i * 26.0f + farOffset;
        if (ly < m_area.top || ly > m_area.bottom) continue;
        bool major = (i % 5 == 0);
        cv.Line(m_area.left + 46.0f, ly, m_area.left + 46.0f + (major ? 16.0f : 8.0f), ly,
                WithAlpha(pal.ink300, major ? 0.7f : 0.35f), 1.0f);
    }
    cv.PopOpacity();
}

void CoverView::PaintEnter(Canvas& cv, float scroll)
{
    const auto& pal = cv.Pal();
    float W = m_area.right - m_area.left;
    float cx = m_area.left + W * 0.5f;

    float sectionTop = m_area.top + m_pageH * 2.0f - scroll;
    if (sectionTop > m_area.bottom) return;

    float appear = Clamp01((m_area.bottom - sectionTop) / (m_pageH * 0.6f));
    float e = ease::OutCubic(appear);
    if (e <= 0.004f) return;

    cv.PushOpacity(e);

    TextStyle ts;
    ts.role = FontRole::Serif; ts.size = 26.0f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    ts.letterSpacing = 4.0f; ts.hAlign = HAlign::Center;
    float ty = sectionTop + m_pageH * 0.38f;
    cv.Text(L"都准备好了", { cx - 300.0f, ty, cx + 300.0f, ty + 40.0f }, ts, pal.ink900);

    TextStyle ss;
    ss.role = FontRole::Sans; ss.size = 13.0f;
    ss.letterSpacing = 1.8f; ss.hAlign = HAlign::Center;
    cv.Text(L"回车或空格，也能直接进", { cx - 300.0f, ty + 44.0f, cx + 300.0f, ty + 66.0f },
            ss, pal.ink500);

    // 印章：随出现进度落下
    float stampT = Clamp01((appear - 0.35f) / 0.5f);
    if (stampT > 0.0f) {
        float se = ease::OutBack(stampT);
        cv.SealStamp(cx + 168.0f, ty + 18.0f, 62.0f * (0.6f + 0.4f * se), L"芙洛理", -11.0f, stampT * 0.85f);
    }

    cv.PopOpacity();

    // 按钮自身带入场，按滚动偏移绘制
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -scroll));
    cv.PushOpacity(e);
    m_enterBtn.Paint(cv);
    cv.PopOpacity();
    cv.PopTransform();
}

} // namespace lj
