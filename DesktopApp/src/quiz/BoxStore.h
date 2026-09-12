#pragma once
// ============================================================
//  BoxStore.h — 题集卡片盒（批次 G，需求 8）
//  自定义「题盒」：正常题盒 / 错题盒；盒内卡片 = 题面 + 答案 + 标签，
//  供抽卡回顾 / 查题展示。落盘 accounts/<账户>/quiz_boxes.json。
//  零依赖（通用 app/Json.h），不耦合 CheckinStore。
// ============================================================
#include <string>
#include <vector>

namespace lj {

struct BoxCard
{
    std::wstring id;        // qc_…
    std::wstring front;     // 题面
    std::wstring back;      // 答案 / 解析
    std::wstring tag;       // 标签（如「资料分析」）
    long long    added = 0;
};

struct QuizBox
{
    std::wstring id;        // bx_…
    std::wstring name;      // 盒名（如「错题盒」「资料分析题盒」）
    int          kind = 0;  // 0=正常题盒 1=错题盒
    long long    created = 0;
    std::vector<BoxCard> cards;
};

class BoxStore
{
public:
    static BoxStore& Instance();

    // 账户切换时注入当前账户根目录（与 QuizStore::SetRoot 同模式）
    void SetRoot(const std::wstring& root) { m_root = root; }

    std::vector<QuizBox> Load();
    void Save(const std::vector<QuizBox>& boxes);

    // 便捷操作（内部 Load→改→Save）
    void AddBox(const std::wstring& name, int kind);
    void DeleteBox(const std::wstring& boxId);
    void RenameBox(const std::wstring& boxId, const std::wstring& name);
    void AddCard(const std::wstring& boxId, const BoxCard& card);
    void DeleteCard(const std::wstring& boxId, const std::wstring& cardId);

    bool empty() const { return m_root.empty(); }

private:
    std::wstring m_root;
    std::wstring Path() const { return m_root + L"quiz_boxes.json"; }
};

} // namespace lj
