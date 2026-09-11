#pragma once
// ============================================================
//  LoginView.h — 账户登录 / 注册 / 切换（#/login）
//  页面状态机（注册独立成页，不再与登录混排）：
//    · PG_LIST  有账户时：仅显示已有账户列表 +「使用其他账户登录」入口
//    · PG_LOGIN 登录页：只有登录表单 +「注册新账户 →」链接切出去
//    · PG_REG   注册页：注册表单 +「← 返回登录」
//    · 登录态：列表底部追加「退出当前账户」
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "app/Data.h"
#include "app/AccountStore.h"
#include "app/Store.h"
#include "core/Hwnd.h"
#include <windows.h>

namespace lj {

class LoginView : public View
{
public:
    const wchar_t* Id() const override { return L"login"; }
    const wchar_t* Title() const override { return L"账户"; }
    bool FullBleed() const override { return true; }   // 登录门槛页：不显示导航条

    void OnEnter() override;
    void OnLeave() override;                            // 离开时收起编辑器
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    enum F { F_LOGIN_USER, F_LOGIN_PASS, F_REG_USER, F_REG_PASS, F_REG_CONFIRM };
    struct Field { D2D1_RECT_F r; F f; bool pass; };
    struct AcctRow { D2D1_RECT_F r; std::wstring name; };
    enum Page { PG_LIST, PG_LOGIN, PG_REG };

    void BeginEdit(const Field& fld);
    void DebugForceOpen() override;   // 截图自检：强制打开登录用户编辑
    void CommitEdit();
    void CancelEdit();

    void DoLogin();
    void DoRegister();
    void DoQuick(const std::wstring& name);
    void DoGuest();
    void DoLogout();
    void EnterHome();

    // 字段缓冲区（单一 EDIT 复用，按字段暂存）
    std::wstring m_loginUser, m_loginPass;
    std::wstring m_regUser, m_regPass, m_regConfirm;
    std::wstring m_errLogin, m_errReg;

    bool     m_built = false;
    float    m_t = 0.0f;
    float    m_topPad = 56.0f;      // 留给窗口按钮条的顶距（全幅页）
    float    m_otherAnim = 0.0f;     // 「使用其他账户登录」点击动效

    // ---- 状态 ----
    Page     m_page = PG_LIST;      // 当前页：账户列表 / 登录 / 注册

    // 布局锚点
    float m_yHead = 0, m_yListHead = 0;
    float m_yLogHead = 0, m_logBottom = 0;      // 登录页
    float m_yRegHead = 0, m_regBottom = 0;      // 注册页
    float m_listBottom = 0;
    D2D1_RECT_F m_loginErrR{}, m_regErrR{};

    std::vector<AcctRow> m_accounts;
    D2D1_RECT_F m_guestRow{};       // 访客入口
    D2D1_RECT_F m_logoutRow{};      // 退出当前账户（仅登录态）
    D2D1_RECT_F m_otherBtn{};       // 「使用其他账户登录」按钮
    D2D1_RECT_F m_backToList{};     // 登录页：「返回账户列表」链接
    D2D1_RECT_F m_linkReg{};        // 登录页：「注册新账户 →」链接（切出去）
    D2D1_RECT_F m_backToLogin{};    // 注册页：「← 返回登录」
    std::vector<Field> m_fields;    // 顺序：0 登录用户 1 登录密码 2 注册用户 3 注册密码 4 确认
    D2D1_RECT_F m_btnLogin{}, m_btnReg{};
    D2D1_RECT_F m_guestLoginRow{};  // 登录页：访客入口（本地模式，不进云端）

    // 密码显隐（眼睛图标）：按字段 F 值索引，仅密码字段使用
    bool m_reveal[5]{};
    D2D1_RECT_F m_eyeRect[5]{};
    bool m_eyeHot = false;

    // 编辑器（v2 统一输入框；5 个字段复用同一个实例）
    FieldEdit m_edit;
    Field  m_editing{};
    bool   m_editingOn = false;

    D2D1_RECT_F m_areaCached{};
    Canvas* m_cvCached = nullptr;
    D2D1_RECT_F m_hot{};            // 当前悬停可点元素
};

} // namespace lj
