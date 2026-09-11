#pragma once
// ============================================================
//  PaperShader.h — 纸基背景（全屏着色器）
//  运行时编译 assets/shaders/paper.hlsl，支持 F5 热重载
// ============================================================
#include "gfx/Graphics.h"
#include "ui/Theme.h"

namespace lj {

class PaperShader
{
public:
    struct Params
    {
        float time = 0.0f;
        float scroll = 0.0f;
        float vignette = 0.14f;
        float grain = 0.030f;
        float parallax = 0.05f;
        float reveal = 1.0f;      // 1 = 完全显影
    };

    bool Initialize(Graphics& gfx);
    void Shutdown();

    // 返回 true 表示重编译成功
    bool Reload(Graphics& gfx);

    void Render(Graphics& gfx, const Palette& pal, const Params& p);

    const std::wstring& LastError() const { return m_lastError; }

private:
    bool Compile(Graphics& gfx);

    struct FrameCB
    {
        float resolution[2];
        float time;
        float scroll;

        float paper[4];
        float paperLo[4];
        float gridColor[4];

        float gridUnit;
        float gridMajor;
        float scale;
        float vignette;

        float grain;
        float parallax;
        float reveal;
        float pad0;
    };

    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
    ComPtr<ID3D11Buffer>       m_cb;
    ComPtr<ID3D11RasterizerState> m_raster;
    ComPtr<ID3D11DepthStencilState> m_depth;
    ComPtr<ID3D11BlendState>   m_blend;

    std::wstring m_path;
    std::wstring m_lastError;
    uint64_t     m_stamp = 0;
};

} // namespace lj