#include "views/LoaderView.h"
#include "app/Data.h"
#include "app/AccountStore.h"

namespace lj {

void LoaderView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_reveal = 0.0f;
    m_done = false;
    m_navigated = false;
    m_barTween.Start(0.0f, 1.0f, 1.15f, ease::InOutCubic, 0.12f);
    m_fade.Start(1.0f, 1.0f, 0.01f, ease::Linear);
}

void LoaderView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_barTween.Update(dt);
    m_fade.Update(dt);

    // 背景从中心显影铺开
    float r = Clamp01((m_t - 0.05f) / 0.9f);
    m_reveal = ease::OutCubic(r);

    if (!m_done && m_barTween.Done()) {
        m_done = true;
        m_fade.Start(1.0f, 0.0f, 0.42f, ease::InOutCubic, 0.18f);
    }
    if (m_done && m_fade.Done() && !m_navigated) {
        m_navigated = true;
        // 开屏播完一律进封面（故事感开屏，无论有无会话）；
        // 封面进入时按会话状态决定去主应用或登录页。
        Go(L"cover");
    }
}

void LoaderView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float W = m_area.right - m_area.left;
    float H = m_area.bottom - m_area.top;
    float cx = m_area.left + W * 0.5f;
    float cy = m_area.top + H * 0.5f;

    cv.PushOpacity(m_fade.running ? m_fade.Value() : 1.0f);

    // ---- 标识：Flori 逐字揭示 ----
    TextStyle brand;
    brand.role = FontRole::Serif;
    brand.size = 62.0f;
    brand.weight = DWRITE_FONT_WEIGHT_BLACK;
    brand.letterSpacing = 14.0f;
    float bw = cv.MeasureWidth(C.appNameLatin, brand);
    cv.CharsReveal(C.appNameLatin, cx - bw * 0.5f, cy - 78.0f, brand,
                   pal.ink900, m_t * 1.05f, 0.075f, 18.0f);

    // ---- 中文名 ----
    float nameA = Clamp01((m_t - 0.42f) / 0.5f);
    if (nameA > 0.0f) {
        TextStyle cn;
        cn.role = FontRole::Serif; cn.size = 20.0f;
        cn.weight = DWRITE_FONT_WEIGHT_BOLD;
        cn.letterSpacing = 12.0f;
        cn.hAlign = HAlign::Center;
        cv.Text(C.appName, { cx - 150.0f, cy - 6.0f, cx + 150.0f + 12.0f, cy + 24.0f },
                cn, WithAlpha(pal.seal, ease::OutCubic(nameA)));
    }

    // ---- 进度条：档案封套式细线 ----
    float barW = (std::min)(320.0f, W * 0.5f);
    float bx = cx - barW * 0.5f;
    float by = cy + 52.0f;
    cv.Line(bx, by, bx + barW, by, WithAlpha(pal.rule, 0.9f), 1.0f);
    float p = m_barTween.Value();
    cv.Line(bx, by, bx + barW * p, by, pal.seal, 2.0f);
    // 游标
    cv.FillRect({ bx + barW * p - 1.0f, by - 5.0f, bx + barW * p + 1.0f, by + 5.0f }, pal.seal);

    // ---- 百分比 + 档案号 ----
    wchar_t buf[64];
    swprintf_s(buf, L"%3d%%", (int)(p * 100.0f + 0.5f));
    TextStyle mono;
    mono.role = FontRole::Mono; mono.size = 11.0f;
    mono.letterSpacing = 1.6f; mono.tabularNums = true;
    mono.hAlign = HAlign::Center;
    cv.Text(buf, { cx - 60.0f, by + 12.0f, cx + 60.0f, by + 30.0f }, mono, pal.ink500);

    TextStyle dn;
    dn.role = FontRole::Mono; dn.size = 10.0f;
    dn.letterSpacing = 2.0f; dn.hAlign = HAlign::Center;
    cv.Text(C.dossierNo, { cx - 200.0f, m_area.bottom - 46.0f, cx + 200.0f, m_area.bottom - 28.0f },
            dn, WithAlpha(pal.ink300, 0.85f));

    // ---- 四角裁切标记：档案边框 ----
    float inset = 26.0f;
    D2D1_RECT_F frame{ m_area.left + inset, m_area.top + inset,
                       m_area.right - inset, m_area.bottom - inset };
    cv.CornerTicks(frame, WithAlpha(pal.ink300, 0.55f * Clamp01(m_t / 0.6f)), 22.0f, 1.2f);

    cv.PopOpacity();
}

} // namespace lj
