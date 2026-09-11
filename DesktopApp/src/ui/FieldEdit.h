#pragma once
// ============================================================
//  FieldEdit.h — 统一单行输入框（v2，全视图共用）
//
//  旧模式（废弃）的病根：编辑态把一个全尺寸 Win32 EDIT 子窗口铺在
//  设计好的输入框上，靠 WM_SETREDRAW(FALSE) 试图禁自绘——但 GDI
//  子窗口一旦显示/聚焦/输入就会自己画白底黑字，且主窗口
//  WS_CLIPCHILDREN 令 D3D 位块传送永远刷不到那块区域，于是
//  「白块盖住输入框、文字打在白块上」。
//
//  v2 模式：
//    · 1×1 WS_EX_TRANSPARENT EDIT 代理：只承担键盘 + IME 焦点，
//      对鼠标完全透明（点击直达主窗口），永远不会盖住字段；
//    · 文字/光标/选区/IME 组合串全部 D3D 自绘（复用 FieldText）；
//    · 点击定位光标、拖拽框选由 HandleMouse 完成；
//    · 1×1 代理随光标/组合串移动，候选窗锚定在光标处。
//
//  坐标约定：box 一律是「视图内容坐标（DIP）」；字段随页面滚动时
//  由视图传入 scrollY（固定面板传 0），组件内部换算客户区坐标。
// ============================================================
#include <windows.h>
#include <string>
#include <functional>
#include "ui/Canvas.h"
#include "ui/Input.h"
#include "ui/FieldText.h"

namespace lj {

class FieldEdit
{
public:
    FieldEdit() = default;
    ~FieldEdit();
    FieldEdit(const FieldEdit&) = delete;
    FieldEdit& operator=(const FieldEdit&) = delete;

    // ---- 生命周期 ----
    // 进入编辑。text 为初值；password=true 时缓冲按圆点绘制。
    // fontDip 为界面字号（IME 组合/候选窗字号跟随，避免脱节）。
    void Begin(const std::wstring& text, bool password = false, float fontDip = 14.0f);
    bool Active() const { return m_active; }
    // 结束编辑；commit=true 时把缓冲写回 out。
    void End(bool commit, std::wstring& out);
    void Cancel();                       // 丢弃缓冲
    std::wstring Text() const;           // 当前缓冲（实时）
    // 编辑中切换密码掩码（密码框「眼睛」显隐用）：只影响绘制映射，不动缓冲
    void SetPassword(bool p) { m_password = p; }
    // 清空缓冲并置光标于起点（编辑态保持；聊天连发 / 白名单添加后用）
    void Clear();

    // ---- 每帧（编辑态）由视图调用 ----
    // 绘制文字/光标/选区/组合串，并把 1×1 代理摆到光标/组合串位置。
    void Paint(Canvas& cv, const D2D1_RECT_F& box, const TextStyle& st,
               const D2D1_COLOR_F& color,
               const std::wstring& placeholder = L"",
               const D2D1_COLOR_F& phColor = {},
               float padLeft = 4.0f, float scrollY = 0.0f);

    // 鼠标 → 光标定位 / 拖拽框选。返回本帧鼠标是否落在本字段内。
    bool HandleMouse(const Input& in, Canvas& cv, const D2D1_RECT_F& box,
                     const TextStyle& st, float scrollY = 0.0f, float padLeft = 4.0f);

    // ---- 键位回调（由共享代理 WndProc 触发）----
    std::function<void()> onEnter;       // 回车（IME 组合中不触发）
    std::function<void()> onEsc;         // Esc   （IME 组合中不触发）
    std::function<void()> onKillFocus;   // 失焦（视图通常提交）

private:
    static LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
    void Ensure();
    void PlaceProxy(float xDip, float yDip, float scrollY);

    HWND    m_edit = nullptr;            // 1×1 透明代理
    WNDPROC m_old  = nullptr;
    bool    m_active = false;
    bool    m_password = false;
    float   m_fontDip = 14.0f;

    std::wstring m_imeComp;              // 未上屏组合串（D3D 自绘）
    int     m_imeCompCaret = 0;
    int     m_dragAnchor = -1;           // 拖拽框选锚点（字符位）
    bool    m_dragging = false;

    int     m_placedX = -1, m_placedY = -1;  // 上次代理摆放（px），避免每帧 SetWindowPos
};

} // namespace lj
