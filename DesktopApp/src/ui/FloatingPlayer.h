#pragma once
// ============================================================
//  FloatingPlayer.h — 全局浮空音乐播放器（#49）
//  背景音乐跨页常驻：无论处在哪个页面，只要有曲目在播/加载/出错，
//  右下角就浮着一个有设计感的小播放器，提供 播放/暂停、关闭、音量。
//  单例；由 App::Frame 在 router 之外转发 Update / Paint（不受页面切换影响）。
// ============================================================
#include "ui/Canvas.h"
#include "ui/Input.h"
#include <string>

namespace lj {

class FloatingPlayer
{
public:
    static FloatingPlayer& Instance();

    // 有曲目（非 Idle）时显示浮层
    bool Active() const;

    // area = 全客户端区域（DIP）。坐标均为屏幕坐标，浮层固定在右下角。
    void Update(float dt, const Input& in, const D2D1_RECT_F& area);
    void Paint(Canvas& cv, const D2D1_RECT_F& area);

    // 本帧是否吃掉了落在浮层上的输入；App 用来屏蔽当前页，避免点击穿透。
    bool ConsumeInput() const { return m_block; }

private:
    FloatingPlayer() = default;
    FloatingPlayer(const FloatingPlayer&) = delete;
    FloatingPlayer& operator=(const FloatingPlayer&) = delete;

    void Layout(const D2D1_RECT_F& area);
    void SetVolumeFromX(float x);
    static bool HitCircle(float x, float y, float cx, float cy, float r);
    static bool PtIn(const D2D1_RECT_F& r, float x, float y);

    // 布局缓存
    D2D1_RECT_F m_card{};
    D2D1_RECT_F m_disc{};
    D2D1_RECT_F m_volTrack{};
    float m_playCx = 0, m_playCy = 0, m_playR = 17.0f;
    float m_closeCx = 0, m_closeCy = 0, m_closeR = 8.0f;

    // 交互态
    bool m_hoverCard = false, m_hoverPlay = false, m_hoverClose = false, m_hoverVol = false;
    bool m_armPlay = false, m_armClose = false;
    bool m_volDrag = false;
    bool m_block = false;
    float m_time = 0.0f;
};

} // namespace lj
