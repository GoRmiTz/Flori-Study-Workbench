#pragma once
// ============================================================
//  ImageViewer.h — 软件内图片查看浮层（云端图片，带鉴权）
//  云端 /media/file/:id 需要 Bearer 令牌，直接 ShellExecute 打开会 401。
//  本组件：后台带令牌拉取字节 → UI 线程 WIC 解码 → ID2D1Bitmap →
//  全屏遮罩 + 等比缩放居中显示 + 标题 + 关闭按钮。
//  单例；任何视图在 Update/Paint 里转发即可（未打开时零开销）。
// ============================================================
#include "core/Common.h"
#include "ui/Canvas.h"
#include "app/Cloud.h"
#include "ui/Input.h"
#include <atomic>
#include <mutex>
#include <wincodec.h>

namespace lj {

class ImageViewer
{
public:
    static ImageViewer& Instance();

    // 打开一张云端图片：后台拉取字节（带 Bearer），UI 线程解码后显示
    void Open(const std::string& id, const std::wstring& title);
    void Close();
    bool Active() const { return m_open; }

    // 视图转发：Update 处理 Esc / 点遮罩关闭；Paint 绘制浮层
    void Update(float dt, const Input& in);
    void Paint(Canvas& cv, const D2D1_RECT_F& area);

private:
    ImageViewer() = default;
    ImageViewer(const ImageViewer&) = delete;
    ImageViewer& operator=(const ImageViewer&) = delete;

    void Decode(ID2D1DeviceContext* dc);   // UI 线程：m_bytes → m_bmp

    std::string  m_id;
    std::wstring m_title;
    bool m_open = false;

    // 后台线程 → UI 线程（原子/锁，遵守「后台不碰 UI」约束）
    std::atomic<int> m_state{ 0 };     // 0 空 1 拉取中 2 字节就绪(待解码) 3 失败
    std::mutex  m_mu;
    std::string m_bytes;
    std::atomic<bool> m_dirty{ false };

    ComPtr<ID2D1Bitmap> m_bmp;         // UI 线程创建（解码成功）
    D2D1_SIZE_U m_px{ 0, 0 };          // 原图像素尺寸（等比缩放用）

    ComPtr<IWICImagingFactory> m_wic;  // 惰性创建

    D2D1_RECT_F m_area{};              // 视口（Paint 时记录，Update 判定点击）
    D2D1_RECT_F m_imgRect{};           // 图片实际绘制区
    D2D1_RECT_F m_closeRect{};         // 关闭按钮
};

} // namespace lj
