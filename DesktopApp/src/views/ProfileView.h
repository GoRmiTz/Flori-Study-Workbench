#pragma once
// ============================================================
//  ProfileView.h — 我的（No.08，个人界面）
//  对应 WebApp vProfile()，并按用户口径扩展为「全内容」个人中心：
//   · 名片区：头像（首字圆章）/ 名号 / UID / 角色 / 个人简介
//   · 资料区：专业（点击循环预设）/ 学校 / 生日 / 性别（点击循环）——
//             文本字段用 Win32 EDIT 就地编辑（含中文 IME），落盘 accounts/<name>/profile.json
//   · 收藏区：跨内容类型（专栏 / 视频 / 图片），点击跳到对应模块
//   · 历史区：最近浏览与播放记录（专栏 / 视频 / 图片 / 音频）
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "app/Data.h"
#include "app/Store.h"
#include "app/AccountStore.h"
#include "quiz/QuizStore.h"
#include "core/Hwnd.h"
#include <windows.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace lj {

class ProfileView : public View
{
public:
    const wchar_t* Id() const override { return L"profile"; }
    const wchar_t* Title() const override { return L"我 的"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    enum PF { PF_BIO = 0, PF_SCHOOL, PF_BIRTH, PF_MAJOR, PF_GENDER, PF_NAME = 5 };

    void PaintTitle(Canvas& cv, float x0, float y, float w);
    void PaintHero(Canvas& cv);
    void PaintStats(Canvas& cv);
    void PaintWeak(Canvas& cv);    // 批次 H：个人错题本（薄弱知识点）
    void PaintFavs(Canvas& cv);
    void PaintHistory(Canvas& cv);
    void PickAvatar();             // 批次 H：选择图片作为头像

    void ReloadAll();
    void SaveProfile();
    void TrySyncProfile();   // 本地保存后异步上推云端（P1-1：离线/超限安全，不弹错）
    void BeginEdit(int field, const D2D1_RECT_F& r);
    void DebugForceOpen() override;   // 截图自检：强制打开简介编辑
    void CommitEdit();
    void CancelEdit();
    void CycleGender();

    // ---- 数据 ----
    AccountProfile m_profile{};
    std::vector<Favorite>    m_favs;
    std::vector<HistoryItem> m_hist;
    int m_colCount = 0;        // 我的专栏数
    int m_checkDays = 0;       // 有打卡记录的天数
    int m_focusMin = 0;        // 累计专注分钟

    // ---- 布局 ----
    float m_contentTop = 0.0f;
    D2D1_RECT_F m_heroRect{};
    D2D1_RECT_F m_avatar{};                 // 头像圆
    D2D1_RECT_F m_bioRect{};                // 简介字段格
    D2D1_RECT_F m_fieldRects[5]{};          // PF_* 五个字段格（PF_BIO 复用 m_bioRect）
    D2D1_RECT_F m_statRects[4]{};
    float m_statsY = 0.0f;
    float m_favTitleY = 0.0f;
    float m_histTitleY = 0.0f;
    // 批次 H：昵称 / 头像 / 错题本
    D2D1_RECT_F m_nameRect{};                 // Hero 名字点击区（就地改名）
    float m_weakTitleY = 0.0f;                // 错题本板块标题 Y
    std::vector<std::pair<std::wstring, int>> m_weakPts;   // 薄弱知识点 → 答错次数
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_avatarBmp;       // 头像位图缓存
    std::wstring m_avatarBmpPath;

    struct FavHit { D2D1_RECT_F r; int idx; };
    std::vector<FavHit> m_favHits;
    std::vector<D2D1_RECT_F> m_histRects;
    int m_hoverFav = -1;
    int m_hoverField = -1;

    Button m_backBtn;
    std::vector<Widget*> m_widgets;

    // ---- 编辑器（v2 统一输入框：1×1 透明代理 + 全 D3D 自绘，无白块）----
    FieldEdit m_edit;
    int     m_editField = -1;
    bool    m_editingOn = false;
    D2D1_RECT_F EditTextBox() const;   // 当前编辑字段的文字框（Update 与 Paint 共用，保证一致）
    Canvas* m_cvCached = nullptr;

    float m_t = 0.0f;
    std::wstring m_toast;
    bool  m_toastBad = false;
    float m_toastT = 0.0f;

    // ---- 档案上云（P1-1）----
    std::mutex         m_pendingMu;        // 保护 m_pendingProfile 的后台写 / UI 读
    AccountProfile     m_pendingProfile{}; // 云端拉取待合并档案
    std::atomic<bool>  m_hasPending{ false };
    std::atomic<bool>  m_cloudPushFailed{ false };  // 云端拒绝上推，待 UI 线程提示一次
};

} // namespace lj
