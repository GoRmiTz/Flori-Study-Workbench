#include "gfx/Graphics.h"
#include <d2d1_1helper.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace lj {

bool Graphics::Initialize(HWND hwnd)
{
    m_hwnd = hwnd;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;   // D2D 

#if defined(_DEBUG)
    //  SDK 

    UINT debugFlags = flags | D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1
    };

    D3D_FEATURE_LEVEL got{};
    HRESULT hr = E_FAIL;
#if defined(_DEBUG)
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, debugFlags,
                           levels, _countof(levels), D3D11_SDK_VERSION,
                           &m_device, &got, &m_context);
    if (FAILED(hr))
#endif
    {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               &m_device, &got, &m_context);
    }
    if (FAILED(hr)) {
        LogHR(L"D3D11CreateDevice(HARDWARE)  WARP", hr);

        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               &m_device, &got, &m_context);
    }
    if (FAILED(hr)) { LogHR(L"D3D11CreateDevice", hr); return false; }
    LogLine(L"[gfx] D3D11  feature level 0x%X", static_cast<unsigned>(got));


    ComPtr<IDXGIDevice1> dxgiDevice;
    LJ_HR_BOOL(m_device.As(&dxgiDevice));
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    LJ_HR_BOOL(dxgiDevice->GetAdapter(&adapter));
    ComPtr<IDXGIFactory2> factory;
    LJ_HR_BOOL(adapter->GetParent(IID_PPV_ARGS(&factory)));

    RECT rc{};
    GetClientRect(hwnd, &rc);
    m_widthPx  = (std::max)(1L, rc.right - rc.left);
    m_heightPx = (std::max)(1L, rc.bottom - rc.top);
    m_dpi = GetDpiForWindow(hwnd);
    if (m_dpi == 0) m_dpi = 96;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width  = m_widthPx;
    sd.Height = m_heightPx;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // blit GDI Win32 EDIT  D3D 

    //  EDIT 

    sd.BufferCount = 1;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_SEQUENTIAL;
    sd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &sd, nullptr, nullptr, &m_swapChain);
    if (FAILED(hr)) {
        // 

        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &sd, nullptr, nullptr, &m_swapChain);
    }
    if (FAILED(hr)) { LogHR(L"CreateSwapChainForHwnd", hr); return false; }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // ---- Direct2D ----
    D2D1_FACTORY_OPTIONS opts{};
#if defined(_DEBUG)
    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
    LJ_HR_BOOL(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(m_d2dFactory.GetAddressOf())));
    LJ_HR_BOOL(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));
    LJ_HR_BOOL(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext));

    // ---- DirectWrite ----
    LJ_HR_BOOL(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1),
                                   reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf())));

    if (!CreateSizeDependent()) return false;
    LogLine(L"[gfx]  %ux%u @ %u dpi", m_widthPx, m_heightPx, m_dpi);

    return true;
}

bool Graphics::CreateSizeDependent()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    LJ_HR_BOOL(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)));
    LJ_HR_BOOL(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv));

    ComPtr<IDXGISurface> surface;
    LJ_HR_BOOL(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)));

    // swap chain 后端缓冲表面：D2D target 必须带 CANNOT_DRAW。
    // 交换链缓冲不能被当作源采样；去掉它 CreateBitmapFromDxgiSurface 会返回
    // E_INVALIDARG(0x80070057) → 启动直接报 "Render device init failed"。
    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    LJ_HR_BOOL(m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_d2dTarget));

    m_d2dContext->SetTarget(m_d2dTarget.Get());
    m_d2dContext->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    m_d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    return true;
}

void Graphics::ReleaseSizeDependent()
{
    if (m_d2dContext) m_d2dContext->SetTarget(nullptr);
    m_d2dTarget.Reset();
    m_rtv.Reset();
    if (m_context) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_context->Flush();
    }
}

bool Graphics::Resize(UINT widthPx, UINT heightPx, UINT dpi)
{
    if (!m_swapChain) return false;
    widthPx  = (std::max)(1u, widthPx);
    heightPx = (std::max)(1u, heightPx);
    if (widthPx == m_widthPx && heightPx == m_heightPx && dpi == m_dpi) return true;

    m_widthPx = widthPx; m_heightPx = heightPx; m_dpi = dpi ? dpi : 96;

    ReleaseSizeDependent();
    HRESULT hr = m_swapChain->ResizeBuffers(0, m_widthPx, m_heightPx, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { LogHR(L"ResizeBuffers", hr); return false; }
    return CreateSizeDependent();
}

void Graphics::BeginFrame()
{
    if (!m_rtv) return;
    ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
    m_context->OMSetRenderTargets(1, rtvs, nullptr);
    //
    float clearColor[4] = { 0.95f, 0.94f, 0.90f, 1.0f };  //
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor); 


    D3D11_VIEWPORT vp{};
    vp.Width  = static_cast<float>(m_widthPx);
    vp.Height = static_cast<float>(m_heightPx);
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

void Graphics::BeginD2D()
{
    if (!m_d2dContext) return;
    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    // 不 clear：交换链表面由 D3D 端 PaperShader 画好纸基，D2D 直接叠 UI。
}

void Graphics::EndD2D()
{
    if (!m_d2dContext) return;
    HRESULT hr = m_d2dContext->EndDraw();
    if (FAILED(hr)) LogHR(L"D2D EndDraw", hr);
}

void Graphics::Present(bool vsync)
{
    if (!m_swapChain) return;
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        LogHR(L"Present", m_device->GetDeviceRemovedReason());
    }
}

void Graphics::Shutdown()
{
    ReleaseSizeDependent();
    m_dwrite.Reset();
    m_d2dContext.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

} // namespace lj
