#include "ui/FloatingPlayer.h"
#include "audio/MusicPlayer.h"
#include "ui/Glyphs.h"
#include <cmath>

namespace lj {

FloatingPlayer& FloatingPlayer::Instance()
{
    static FloatingPlayer s;
    return s;
}

bool FloatingPlayer::Active() const
{
    return MusicPlayer::Instance().GetState() != MusicPlayer::State::Idle;
}

bool FloatingPlayer::HitCircle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool FloatingPlayer::PtIn(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void FloatingPlayer::Layout(const D2D1_RECT_F& area)
{
    const float Wc = 332.0f, Hc = 78.0f;
    float x = area.right - Wc - 22.0f;
    float y = area.bottom - Hc - 22.0f;
    if (x < 12.0f) x = 12.0f;
    if (y < 12.0f) y = 12.0f;

    m_card = { x, y, x + Wc, y + Hc };

    const float d = 46.0f;
    float dy = y + (Hc - d) * 0.5f;
    m_disc = { x + 16.0f, dy, x + 16.0f + d, dy + d };

    // 播放按钮（圆形，偏右下）
    m_playCx = x + Wc - 24.0f;
    m_playCy = y + 46.0f;
    // 关闭 ×（小，右上角）
    m_closeCx = x + Wc - 18.0f;
    m_closeCy = y + 16.0f;

    // 音量滑条（中下，介于唱片与播放按钮之间）
    float vx0 = m_disc.right + 14.0f;
    float vx1 = m_playCx - m_playR - 16.0f;
    float vy = y + 68.0f;
    m_volTrack = { vx0, vy - 9.0f, vx1, vy + 9.0f };
}

void FloatingPlayer::SetVolumeFromX(float x)
{
    float t = (x - m_volTrack.left) / (m_volTrack.right - m_volTrack.left);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    MusicPlayer::Instance().SetVolume(t * t);          // 平方映射（与房间卡一致）
}

void FloatingPlayer::Update(float dt, const Input& in, const D2D1_RECT_F& area)
{
    m_block = false;
    if (!Active()) return;
    Layout(area);
    m_time = in.time;

    float mx = in.mouseX, my = in.mouseY;
    m_hoverCard  = PtIn(m_card, mx, my);
    m_hoverPlay  = HitCircle(mx, my, m_playCx, m_playCy, m_playR + 3.0f);
    m_hoverClose = HitCircle(mx, my, m_closeCx, m_closeCy, m_closeR + 3.0f);
    float vy = (m_volTrack.top + m_volTrack.bottom) * 0.5f;
    m_hoverVol = (mx >= m_volTrack.left - 8.0f && mx <= m_volTrack.right + 8.0f &&
                  my >= vy - 10.0f && my <= vy + 10.0f);

    // 播放 / 暂停
    if (in.pressed && m_hoverPlay) m_armPlay = true;
    if (in.released) {
        if (m_armPlay) {
            if (m_hoverPlay) MusicPlayer::Instance().Toggle();
            m_armPlay = false;
            m_block = true;
        }
    }

    // 关闭（停止并隐藏）
    if (in.pressed && m_hoverClose) m_armClose = true;
    if (in.released) {
        if (m_armClose) {
            if (m_hoverClose) MusicPlayer::Instance().Stop();
            m_armClose = false;
            m_block = true;
        }
    }

    // 音量（拖动）
    if (in.pressed && m_hoverVol) { m_volDrag = true; SetVolumeFromX(mx); m_block = true; }
    if (m_volDrag) {
        SetVolumeFromX(mx);
        if (in.released) m_volDrag = false;
        m_block = true;
    }

    // 落在浮层上的任何按下都归浮层，屏蔽页面点击穿透
    if (in.pressed && m_hoverCard) m_block = true;
}

void FloatingPlayer::Paint(Canvas& cv, const D2D1_RECT_F& area)
{
    if (!Active()) return;
    Layout(area);
    const auto& pal = cv.Pal();

    auto st = MusicPlayer::Instance().GetState();
    bool playing = (st == MusicPlayer::State::Playing);

    // ---- 卡片：投影 + 纸面 + 朱砂描边 ----
    cv.FillRoundRect({ m_card.left + 3.0f, m_card.top + 5.0f, m_card.right + 3.0f, m_card.bottom + 5.0f },
                     12.0f, WithAlpha(pal.ink900, pal.dark ? 0.28f : 0.10f));
    cv.FillRoundRect(m_card, 12.0f, pal.paperHi);
    cv.StrokeRoundRect(m_card, 12.0f, WithAlpha(pal.seal, 0.30f), shape::kHair + 0.3f);

    // ---- 左缘色条（档案分类标记感）----
    cv.FillRoundRect({ m_card.left, m_card.top, m_card.left + 4.0f, m_card.bottom }, 12.0f,
                     WithAlpha(pal.seal, 0.85f));

    // ---- 唱片：朱砂圆角方 + 均衡器动效 ----
    cv.FillRoundRect(m_disc, 9.0f, pal.seal);
    cv.StrokeRoundRect(m_disc, 9.0f, WithAlpha(pal.sealLo, 0.85f), 1.2f);
    {
        float dcx = (m_disc.left + m_disc.right) * 0.5f;
        float dcy = (m_disc.top + m_disc.bottom) * 0.5f;
        const int nBars = 4;
        float bw = 4.0f, gap = 4.5f;
        float total = nBars * bw + (nBars - 1) * gap;
        for (int i = 0; i < nBars; ++i) {
            float phase = (float)i * 0.9f;
            float amp = playing ? (0.35f + 0.65f * fabsf(sinf(m_time * 3.0f + phase)))
                                : 0.12f;
            float bh = (m_disc.bottom - m_disc.top) * 0.62f * amp;
            float bx = dcx - total * 0.5f + i * (bw + gap);
            float by = dcy - bh * 0.5f;
            cv.FillRoundRect({ bx, by, bx + bw, by + bh }, 2.0f,
                             WithAlpha(pal.paperHi, playing ? 1.0f : 0.6f));
        }
    }

    // ---- 曲名（超宽省略号）----
    std::wstring title = MusicPlayer::Instance().GetTitle();
    TextStyle tt; tt.role = FontRole::Sans; tt.size = 13.5f;
    tt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; tt.vAlign = VAlign::Middle;
    float titleR = m_playCx - m_playR - 14.0f;
    D2D1_RECT_F titleBox{ m_disc.right + 14.0f, m_card.top + 14.0f, titleR, m_card.top + 34.0f };
    if (title.empty()) title = L"未命名曲目";
    // 省略号裁剪
    std::wstring shown = title;
    while (shown.size() > 1 && cv.MeasureWidth(shown + L"…", tt) > (titleBox.right - titleBox.left))
        shown.pop_back();
    if (shown != title) shown += L"…";
    cv.Text(shown, titleBox, tt, pal.ink900);

    // ---- 状态行 ----
    std::wstring status;
    D2D1_COLOR_F sc = pal.ink500;
    if (st == MusicPlayer::State::Loading)      { status = L"加载中…"; sc = pal.ink500; }
    else if (st == MusicPlayer::State::Playing) { status = L"正在播放"; sc = pal.jade; }
    else if (st == MusicPlayer::State::Paused)  { status = L"已暂停"; sc = pal.ink500; }
    else if (st == MusicPlayer::State::Error)   { status = L"播放失败 · 点 × 关闭"; sc = pal.vermilion; }
    TextStyle ss; ss.role = FontRole::Mono; ss.size = 10.5f; ss.letterSpacing = 0.6f;
    ss.vAlign = VAlign::Middle;
    cv.Text(status, { m_disc.right + 14.0f, m_card.top + 36.0f, titleR, m_card.top + 54.0f }, ss, sc);

    // ---- 音量滑条（中下）----
    {
        float vol = MusicPlayer::Instance().GetVolume();
        float vp = vol > 0.0f ? sqrtf(vol) : 0.0f;
        float vx0 = m_volTrack.left, vx1 = m_volTrack.right;
        float vy = (m_volTrack.top + m_volTrack.bottom) * 0.5f;
        cv.FillRoundRect({ vx0, vy - 2.0f, vx1, vy + 2.0f }, 2.0f, WithAlpha(pal.rule, 0.6f));
        float fx = vx0 + (vx1 - vx0) * vp;
        if (fx > vx0 + 2.0f)
            cv.FillRoundRect({ vx0, vy - 2.0f, fx, vy + 2.0f }, 2.0f, pal.seal);
        cv.FillCircle(fx, vy, 5.0f, (m_volDrag || m_hoverVol) ? pal.seal : pal.ink300);
        cv.StrokeCircle(fx, vy, 5.0f, pal.seal, 1.2f);
    }

    // ---- 播放 / 暂停 圆形按钮 ----
    {
        auto pbg = MixColor(pal.seal, pal.sealLo, m_hoverPlay * 0.6f);
        cv.FillCircle(m_playCx, m_playCy, m_playR, pbg);
        cv.StrokeCircle(m_playCx, m_playCy, m_playR, WithAlpha(pal.sealLo, 0.7f), 1.2f);
        float ih = m_playR * 1.0f;
        D2D1_RECT_F gb{ m_playCx - ih * 0.5f, m_playCy - ih * 0.5f,
                        m_playCx + ih * 0.5f, m_playCy + ih * 0.5f };
        if (playing) PaintPauseGlyph(cv, gb, pal.paperHi);
        else         PaintPlayGlyph(cv, gb, pal.paperHi);
    }

    // ---- 关闭 × ----
    {
        cv.FillCircle(m_closeCx, m_closeCy, m_closeR,
                      m_hoverClose ? WithAlpha(pal.vermilion, 0.20f) : WithAlpha(pal.ink900, 0.06f));
        float s = m_closeR * 0.5f;
        cv.Line(m_closeCx - s, m_closeCy - s, m_closeCx + s, m_closeCy + s, pal.ink500, 1.4f);
        cv.Line(m_closeCx - s, m_closeCy + s, m_closeCx + s, m_closeCy - s, pal.ink500, 1.4f);
    }
}

} // namespace lj
