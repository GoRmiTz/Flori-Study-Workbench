#pragma once
// ============================================================
//  FriendView.h — 好友（No.09，#37）
//  好友列表（云端 REST）+ 添加/删除好友 + 私聊浮层（实时 WS）。
//  进度分享：好友卡片展示对方公开统计（打卡天数 / 专注分钟）。
//  输入框 v2：1×1 透明代理 EDIT + D3D 自绘（FieldEdit，无白块）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "app/Cloud.h"
#include "app/Json.h"
#include "net/Realtime.h"
#include <vector>
#include <mutex>
#include <atomic>

namespace lj {

class FriendView : public View
{
public:
    const wchar_t* Id() const override { return L"friend"; }
    const wchar_t* Title() const override { return L"好 友"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    struct FriendItem {
        std::wstring uid, username;
        bool online = false;
        long long days = 0, focusMin = 0;
    };
    struct DmMsg { bool mine = false; std::wstring text; long long ts = 0; };

    // 好友列表（后台拉取 → 受锁缓冲 → UI 快照）
    std::mutex  m_mu;
    std::vector<FriendItem> m_src;
    std::atomic<bool> m_dirty{ false };
    std::vector<FriendItem> m_friends;
    bool m_fetched = false;
    std::wstring m_hint; long long m_hintT = 0;   // 添加/删除结果提示

    // 布局
    float m_contentTop = 0, m_listY = 0;
    D2D1_RECT_F m_addBox{}, m_addBtn{};
    std::vector<D2D1_RECT_F> m_cardRects, m_delBtns, m_dmBtns;
    int m_hover = -1;
    Button m_backBtn;
    std::vector<Widget*> m_widgets;
    float m_t = 0.0f;

    // 添加框（v2 统一输入框）
    FieldEdit m_addEdit;
    bool m_addOn = false; std::wstring m_addText;

    // 私聊浮层
    bool m_dmOpen = false;
    FriendItem m_dmPeer;
    std::mutex  m_dmMu;
    std::vector<DmMsg> m_dmSrc;
    std::atomic<bool> m_dmDirty{ false };
    std::vector<DmMsg> m_dmMsgs;
    float m_dmScroll = 0;
    D2D1_RECT_F m_dmCard{}, m_dmList{}, m_dmInput{}, m_dmSend{}, m_dmClose{};
    FieldEdit m_dmEdit;                     // v2 统一输入框（私聊输入）
    bool m_dmOn = false; std::wstring m_dmText;

    void FetchFriends();            // 后台拉取 GET /friends
    void Snapshot();                // 来源 → 快照
    void SetHint(const std::wstring& s);

    void BeginAddEdit(); void EndAddEdit(bool commit);
    void BeginDmEdit();  void EndDmEdit(bool send);
    void OpenDm(const FriendItem& f); void CloseDm();
    void SendDmText(const std::wstring& t);

    void OnNetDm(const lj::json::JVal& m);         // chat:dm 下行
    void OnNetDmHistory(const lj::json::JVal& m);  // dm:history 下行

    Canvas* m_cvCached = nullptr;   // HandleMouse 测量宽度需要（Layout 时缓存）
};

} // namespace lj
