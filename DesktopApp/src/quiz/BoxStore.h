#pragma once
// ============================================================
//  BoxStore.h — 题集卡片盒 v2（批次 G2，三层结构）
//  题集 Set（房子）→ 题盒 Box（盒子）→ 卡片 Card（方块）。
//  卡片带难度(1..5)/重复错次/最后复习时间（红色警示因子）。
//  落盘 accounts/<账户>/quiz_sets.json；旧 quiz_boxes.json 自动迁移。
//  完整需求见 docs/题集卡片盒·开发文档.md（唯一基准，细节不得丢失）。
// ============================================================
#include <string>
#include <vector>

namespace lj {

struct QCard
{
    std::wstring id;          // qc_…
    std::wstring front;       // 题面
    std::wstring back;        // 答案 / 解析
    std::wstring tag;         // 标签
    int       difficulty = 3;   // 难度 1..5（边框色/角标/动效强度）
    int       wrongCount = 0;  // 重复答错次数（变红因子）
    long long lastReview = 0;  // 最后复习时间（变红因子）
    long long added = 0;
};

struct QuizBox
{
    std::wstring id;          // bx_…
    std::wstring name;
    int       kind = 0;        // 0=正常题盒 1=错题盒
    long long created = 0;
    std::vector<QCard> cards;
};

struct QuizSet
{
    std::wstring id;          // st_…
    std::wstring name;        // 题集名（学习区域，如「考公」）
    long long created = 0;
    std::vector<QuizBox> boxes;
};

class BoxStore
{
public:
    static BoxStore& Instance();

    void SetRoot(const std::wstring& root) { m_root = root; }

    // 整树读写（视图持有内存态，改完统一落盘也行）
    std::vector<QuizSet> Load();
    void Save(const std::vector<QuizSet>& sets);

    // ---- 题集 ----
    void AddSet(const std::wstring& name);
    void DeleteSet(const std::wstring& setId);
    void RenameSet(const std::wstring& setId, const std::wstring& name);

    // ---- 题盒 ----
    void AddBox(const std::wstring& setId, const std::wstring& name, int kind);
    void DeleteBox(const std::wstring& boxId);
    void RenameBox(const std::wstring& boxId, const std::wstring& name);

    // ---- 卡片 ----
    void AddCard(const std::wstring& boxId, const QCard& card);
    void DeleteCard(const std::wstring& boxId, const std::wstring& cardId);
    void UpdateCard(const std::wstring& boxId, const QCard& card);
    void MoveCard(const std::wstring& fromBox, const std::wstring& cardId,
                  const std::wstring& toBox);   // 跨盒移动（T4/T9）

    bool empty() const { return m_root.empty(); }

private:
    std::wstring m_root;
    std::wstring Path() const { return m_root + L"quiz_sets.json"; }
    std::wstring OldPath() const { return m_root + L"quiz_boxes.json"; }
    void MigrateFromV1(std::vector<QuizSet>& sets);   // 旧版两层数据 → 默认题集
};

} // namespace lj
