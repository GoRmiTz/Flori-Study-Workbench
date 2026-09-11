#pragma once
// ============================================================
//  Graphics.h — 渲染底座
//  D3D11 设备 + DXGI 翻转链 + Direct2D 设备上下文 + DirectWrite
//  同一块后台缓冲：先由 D3D11 跑着色器画纸基，再由 D2D 叠矢量 UI。
// ============================================================
#include "core/Common.h"
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite_1.h>

namespace lj {

class Graphics
{
public:
    bool Initialize(HWND hwnd);
    void Shutdown();

    // 尺寸变化（像素）。dpi 由窗口传入。
    bool Resize(UINT widthPx, UINT heightPx, UINT dpi);

    void BeginFrame();                 // 绑定 RTV，供 D3D 绘制
    void BeginD2D();                   // 切到 D2D（在 D3D 之后调用）
    void EndD2D();
    void Present(bool vsync = true);

    ID3D11Device*         Device()   const { return m_device.Get(); }
    ID3D11DeviceContext*  Context()  const { return m_context.Get(); }
    ID3D11RenderTargetView* RTV()    const { return m_rtv.Get(); }
    ID2D1DeviceContext*   D2D()      const { return m_d2dContext.Get(); }
    IDWriteFactory1*      DWrite()   const { return m_dwrite.Get(); }
    IDXGISwapChain1*      SwapChain()const { return m_swapChain.Get(); }

    UINT  WidthPx()  const { return m_widthPx; }
    UINT  HeightPx() const { return m_heightPx; }
    UINT  Dpi()      const { return m_dpi; }
    float Scale()    const { return m_dpi / 96.0f; }
    // 逻辑尺寸（DIP），UI 布局一律用它
    float WidthDip()  const { return m_widthPx  / Scale(); }
    float HeightDip() const { return m_heightPx / Scale(); }

    bool Ready() const { return m_swapChain != nullptr; }

private:
    bool CreateSizeDependent();
    void ReleaseSizeDependent();

    HWND m_hwnd = nullptr;
    UINT m_widthPx = 0, m_heightPx = 0, m_dpi = 96;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain1>        m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    ComPtr<ID2D1Factory1>          m_d2dFactory;
    ComPtr<ID2D1Device>            m_d2dDevice;
    ComPtr<ID2D1DeviceContext>     m_d2dContext;
    ComPtr<ID2D1Bitmap1>           m_d2dTarget;   // swap chain 表面（CANNOT_DRAW，D2D 直绘 UI 的最终目标）

    ComPtr<IDWriteFactory1>        m_dwrite;
};

} // namespace lj
