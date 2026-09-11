#include "core/Screenshot.h"
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

namespace lj {

bool SaveBackBufferPNG(Graphics& gfx, const std::wstring& path)
{
    if (!gfx.Ready()) return false;

    ComPtr<ID3D11Texture2D> back;
    if (FAILED(gfx.SwapChain()->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;

    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gfx.Device()->CreateTexture2D(&sd, nullptr, &staging))) return false;
    gfx.Context()->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(gfx.Context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wic));
    if (FAILED(hr)) { gfx.Context()->Unmap(staging.Get(), 0); return false; }

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;

    bool ok = false;
    if (SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(desc.Width, desc.Height)))
    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->SetPixelFormat(&fmt))) {
            // 逐行写，避免 pitch 不一致
            std::vector<BYTE> row(desc.Width * 4);
            bool wrote = true;
            for (UINT y = 0; y < desc.Height && wrote; ++y) {
                const BYTE* src = static_cast<const BYTE*>(map.pData) + (size_t)y * map.RowPitch;
                memcpy(row.data(), src, desc.Width * 4);
                for (UINT x = 0; x < desc.Width; ++x) row[x * 4 + 3] = 255;   // 强制不透明
                wrote = SUCCEEDED(frame->WritePixels(1, desc.Width * 4,
                                                     (UINT)row.size(), row.data()));
            }
            if (wrote && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()))
                ok = true;
        }
    }

    gfx.Context()->Unmap(staging.Get(), 0);
    if (ok) LogLine(L"[shot] 已保存 %s", path.c_str());
    else    LogLine(L"[shot] 保存失败 %s", path.c_str());
    return ok;
}

} // namespace lj
