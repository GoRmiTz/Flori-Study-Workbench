#pragma once
// ============================================================
//  FieldText.h — 编辑态文字绘制助手
//  隐藏的 Win32 EDIT 仅作「输入法 / 键盘捕获代理」（1x1 透明），
//  真正可见的文字与光标由软件自身的 DirectWrite 字体画在设计框内，
//  从而：① 不再有覆盖边框的矩形块；② 字体与软件其它文字完全一致；
//  ③ 中文输入法照常工作（EDIT 仍持有焦点）。
// ============================================================
#include <windows.h>
#include <string>
#include "ui/Canvas.h"

namespace lj {

// 在 textBox 内用软件字体绘制编辑态文本；caret>=0 时画闪烁光标。
// textBox 应与「非编辑态」绘制所用的文字框完全一致，避免点击后文字跳动。
// selStart>=0 时绘制选中高亮（selStart..selEnd，反色文字 + 半透明底），
// 供隐藏代理 EDIT 的「鼠标框选」在 D3D 层可见（代理 1x1 的选区不可见）。
void PaintFieldEdit(Canvas& cv,
                    const D2D1_RECT_F& textBox,
                    const TextStyle& st,
                    const std::wstring& text,
                    const D2D1_COLOR_F& color,
                    int caret,            // -1 不画光标
                    float padLeft = 4.0f,
                    int selStart = -1,    // -1 不画选区
                    int selEnd = -1);

// 带「IME 组合串」的编辑态绘制（P1-2）。
// 中文输入法在提交前，拼音/候选文本存在 IME 上下文里而非 EDIT 文本缓冲，
// 且代理 EDIT 被 WM_SETREDRAW(FALSE) 禁止自绘 —— 若不自行绘制，用户打拼音时屏幕上一片空白。
//   text       已提交文本（ReadEditBuffer 所得）
//   caret      已提交文本内的插入点（组合串插在此处）
//   comp       未提交的 IME 组合串（画虚线下划线以区别于已提交文字）
//   compCaret  组合串内的光标字符位置
// 返回：组合串起点的绝对 x（DIP）——供 ImmSetCandidateWindow 定位候选窗，使其贴着拼音而非飘到框首。
float PaintFieldEditIme(Canvas& cv,
                        const D2D1_RECT_F& textBox,
                        const TextStyle& st,
                        const std::wstring& text,
                        int caret,
                        const std::wstring& comp,
                        int compCaret,
                        const D2D1_COLOR_F& color,
                        float padLeft = 4.0f);

// ---- P1-2 中文 IME 合成共享助手（所有代理 EDIT 视图通用）----
// 从指定 EDIT 窗口的 IME 上下文读取未提交组合串与其内部光标位置。
// 返回是否处于合成状态；comp/compCaret 仅在返回 true 时有效。
bool ImeReadComposition(HWND h, std::wstring& comp, int& compCaret);

// 更新 IME 组合窗与候选窗位置，避免候选窗飘到输入框左上角。
//   h             当前持有输入焦点的 EDIT 代理
//   compX_client  组合串起点相对 EDIT 客户区的 x（DIP）
//   boxH          输入框客户区高度（DIP）
//   dpi           当前 DPI
void ImeSetCandidatePos(HWND h, float compX_client, float boxH, UINT dpi);

// 取消当前 IME 合成（用于退出编辑/发送前清理候选窗）
void ImeCancelComposition(HWND h);

// 读取隐藏 EDIT 代理的当前文本 / 光标位置（供 D3D 绘制用）
std::wstring ReadEditBuffer(HWND h);
int EditCaretPos(HWND h);
// 读取隐藏 EDIT 代理的当前选区（EM_GETSEL；未选中时 a==b）
void EditSelRange(HWND h, int& a, int& b);

// 由 x 坐标换算字符位置（用于点击定位光标 / 拖拽框选）。二分逼近，左对齐文本。
int CharIndexAtX(Canvas& cv, const std::wstring& text, const TextStyle& st,
                 float textLeft, float x);

} // namespace lj
