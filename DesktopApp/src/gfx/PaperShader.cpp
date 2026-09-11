#include "gfx/PaperShader.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace lj {

// Fallback embedded HLSL in case assets/shaders/paper.hlsl is missing
static const char* kFallbackHLSL = R"(
cbuffer FrameCB : register(b0) {
    float2 uResolution; float uTime; float uScroll;
    float4 uPaper; float4 uPaperLo; float4 uGridColor;
    float uGridUnit; float uGridMajor; float uScale; float uVignette;
    float uGrain; float uParallax; float uReveal; float uPad0;
};
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o; float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv; o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float r = saturate(length(i.uv - 0.5) * 1.35);
    float3 c = lerp(uPaper.rgb, uPaperLo.rgb, r * 0.85);
    return float4(c * (1.0 - uVignette * r * r), 1);
}
)";

bool PaperShader::Initialize(Graphics& gfx)
{
    m_path = ResolveAsset(L"shaders\\paper.hlsl");

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(FrameCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    LJ_HR_BOOL(gfx.Device()->CreateBuffer(&bd, nullptr, &m_cb));

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    LJ_HR_BOOL(gfx.Device()->CreateRasterizerState(&rd, &m_raster));

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    LJ_HR_BOOL(gfx.Device()->CreateDepthStencilState(&dd, &m_depth));

    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = FALSE;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    LJ_HR_BOOL(gfx.Device()->CreateBlendState(&bl, &m_blend));

    return Compile(gfx);
}

bool PaperShader::Compile(Graphics& gfx)
{
    std::string src;
    bool fromFile = ReadTextFile(m_path, src);
    if (!fromFile) {
        LogLine(L"[shader] %s not found, using embedded fallback", m_path.c_str());
        src = kFallbackHLSL;
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    HRESULT hr = D3DCompile(src.data(), src.size(), "paper.hlsl", nullptr, nullptr,
                            "VSMain", "vs_5_0", flags, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        std::string e = errBlob ? std::string(static_cast<const char*>(errBlob->GetBufferPointer()),
                                              errBlob->GetBufferSize())
                                : "unknown";
        m_lastError.assign(e.begin(), e.end());
        LogLine(L"[shader] VS compile failed: %s", m_lastError.c_str());
        return false;
    }
    errBlob.Reset();
    hr = D3DCompile(src.data(), src.size(), "paper.hlsl", nullptr, nullptr,
                    "PSMain", "ps_5_0", flags, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        std::string e = errBlob ? std::string(static_cast<const char*>(errBlob->GetBufferPointer()),
                                              errBlob->GetBufferSize())
                                : "unknown";
        m_lastError.assign(e.begin(), e.end());
        LogLine(L"[shader] PS compile failed: %s", m_lastError.c_str());
        return false;
    }

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    LJ_HR_BOOL(gfx.Device()->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs));
    LJ_HR_BOOL(gfx.Device()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps));

    m_vs = vs; m_ps = ps;
    m_stamp = FileWriteTime(m_path);
    m_lastError.clear();
    LogLine(L"[shader] paper.hlsl compiled %s", fromFile ? L"" : L"(embedded)");
    return true;
}

bool PaperShader::Reload(Graphics& gfx)
{
    return Compile(gfx);
}

void PaperShader::Render(Graphics& gfx, const Palette& pal, const Params& p)
{
    if (!m_vs || !m_ps) return;
    auto* ctx = gfx.Context();

    FrameCB cb{};
    cb.resolution[0] = static_cast<float>(gfx.WidthPx());
    cb.resolution[1] = static_cast<float>(gfx.HeightPx());
    cb.time   = p.time;
    cb.scroll = p.scroll * gfx.Scale();

    auto put = [](float* dst, const D2D1_COLOR_F& c) {
        dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = c.a;
    };
    put(cb.paper,   pal.paperHi);
    put(cb.paperLo, pal.paperDeep);
    put(cb.gridColor, pal.ruleStrong);
    cb.gridColor[3] = pal.ruleGridAlpha * 4.0f;

    cb.gridUnit  = shape::kGridUnit;
    cb.gridMajor = shape::kGridMajor;
    cb.scale     = gfx.Scale();
    cb.vignette  = p.vignette;
    cb.grain     = p.grain;
    cb.parallax  = p.parallax;
    cb.reveal    = p.reveal;

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &cb, sizeof(cb));
        ctx->Unmap(m_cb.Get(), 0);
    }

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { m_cb.Get() };
    ctx->VSSetConstantBuffers(0, 1, cbs);
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->RSSetState(m_raster.Get());
    ctx->OMSetDepthStencilState(m_depth.Get(), 0);
    float bf[4]{};
    ctx->OMSetBlendState(m_blend.Get(), bf, 0xFFFFFFFF);

    ctx->Draw(3, 0);
    // Unbind before returning to D2D
    ctx->VSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
}

void PaperShader::Shutdown()
{
    m_vs.Reset(); m_ps.Reset(); m_cb.Reset();
    m_raster.Reset(); m_depth.Reset(); m_blend.Reset();
}

} // namespace lj