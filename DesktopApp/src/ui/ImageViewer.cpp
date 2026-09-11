#include "ui/ImageViewer.h"

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
} // namespace

ImageViewer& ImageViewer::Instance()
{
    static ImageViewer s;
    return s;
}

void ImageViewer::Open(const std::string& id, const std::wstring& title)
{
    m_id = id;
    m_title = title;
    m_bmp.Reset();
    m_px = { 0, 0 };
    m_open = true;
    m_state.store(1);
    m_dirty.store(false);
    Cloud::RunAsync([this, id]() {
        net::Response r = Cloud::Instance().GetMediaFile(id);
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_bytes = r.body;
        }
        m_state.store(r.Ok() ? 2 : 3);   // 2=字节就绪(待 UI 解码) 3=失败
        m_dirty.store(true);
    });
}

void ImageViewer::Close()
{
    m_open = false;
    m_bmp.Reset();
    m_state.store(0);
    m_dirty.store(false);
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_bytes.clear();
    }
}

// UI 线程：字节 → WIC 解码 → ID2D1Bitmap
void ImageViewer::Decode(ID2D1DeviceContext* dc)
{
    if (!dc) { m_state.store(3); return; }
    std::string bytes;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        bytes = m_bytes;
    }
    if (bytes.empty()) { m_state.store(3); return; }

    if (!m_wic) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_wic));
        if (FAILED(hr)) { m_state.store(3); return; }
    }

    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) { m_state.store(3); return; }
    ULONG written = 0;
    stream->Write(bytes.data(), (ULONG)bytes.size(), &written);
    LARGE_INTEGER z{};
    stream->Seek(z, STREAM_SEEK_SET, nullptr);

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(m_wic->CreateDecoderFromStream(stream.Get(), nullptr,
                                              WICDecodeMetadataCacheOnLoad, &dec))) { m_state.store(3); return; }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) { m_state.store(3); return; }
    UINT pw = 0, ph = 0;
    if (FAILED(frame->GetSize(&pw, &ph))) { m_state.store(3); return; }
    m_px = { pw, ph };

    m_bmp.Reset();
    if (FAILED(dc->CreateBitmapFromWicBitmap(frame.Get(), nullptr, &m_bmp))) { m_state.store(3); return; }
    m_state.store(2);
}

void ImageViewer::Update(float dt, const Input& in)
{
    (void)dt;
    if (!m_open) return;
    if (in.keyDown[VK_ESCAPE]) { Close(); return; }
    if (in.clicked) {
        // 点图片区外（遮罩/关闭按钮）关闭；点图片本身不关（可细看）
        if (m_closeRect.right > m_closeRect.left && Hit(m_closeRect, in.mouseX, in.mouseY)) { Close(); return; }
        if (!Hit(m_imgRect, in.mouseX, in.mouseY)) Close();
    }
}

void ImageViewer::Paint(Canvas& cv, const D2D1_RECT_F& area)
{
    if (!m_open) return;
    m_area = area;
    const auto& pal = cv.Pal();

    // 字节就绪 → 解码（WIC + CreateBitmapFromWicBitmap 必须在 UI 线程/D2D 上下文）
    if (m_dirty.exchange(false)) Decode(cv.DC());

    // 全屏遮罩
    cv.FillRect(area, D2D1::ColorF(0.06f, 0.05f, 0.04f, 0.78f));

    const float areaW = area.right - area.left;
    const float areaH = area.bottom - area.top;
    const float cx = (area.left + area.right) * 0.5f;
    const float cy = (area.top + area.bottom) * 0.5f;
    const float maxW = areaW * 0.70f;
    const float maxH = areaH * 0.72f;

    // 状态文案
    TextStyle st; st.role = FontRole::Sans; st.size = 13.0f; st.hAlign = HAlign::Center; st.vAlign = VAlign::Middle;
    if (m_state.load() == 1) {
        cv.Text(L"图片加载中…", { cx - 120.0f, cy - 20.0f, cx + 120.0f, cy + 20.0f }, st, pal.ink300);
        return;
    }
    if (m_state.load() == 3) {
        cv.Text(L"图片加载失败（未连上服务端或权限不足）", { cx - 260.0f, cy - 20.0f, cx + 260.0f, cy + 20.0f }, st, pal.vermilion);
        return;
    }
    if (m_state.load() == 2 && m_bmp) {
        // 等比缩放（最大 maxW×maxH，原图更小则原尺寸）
        float scale = 1.0f;
        if (m_px.width > 0 && m_px.height > 0) {
            float sw = maxW / (float)m_px.width;
            float sh = maxH / (float)m_px.height;
            scale = (std::min)(1.0f, (std::min)(sw, sh));
        }
        float w = (float)m_px.width * scale;
        float h = (float)m_px.height * scale;
        m_imgRect = { cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f };

        // 底衬 + 图片
        D2D1_RECT_F pad{ m_imgRect.left - 14.0f, m_imgRect.top - 46.0f,
                         m_imgRect.right + 14.0f, m_imgRect.bottom + 14.0f };
        cv.FillRoundRect(pad, 8.0f, D2D1::ColorF(0.10f, 0.09f, 0.08f, 0.96f));
        cv.StrokeRoundRect(pad, 8.0f, WithAlpha(pal.rule, 0.5f), shape::kHair);

        ID2D1DeviceContext* dc = cv.DC();
        if (dc) {
            D2D1_RECT_F src{ 0.0f, 0.0f, (float)m_px.width, (float)m_px.height };
            dc->DrawBitmap(m_bmp.Get(), m_imgRect, 1.0f,
                           D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, src);
        }

        // 标题（底衬上方）
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(m_title, { pad.left + 12.0f, pad.top + 8.0f, pad.right - 44.0f, pad.top + 38.0f }, ts, pal.ink900);

        // 关闭按钮（右上 ×）
        m_closeRect = { pad.right - 34.0f, pad.top + 6.0f, pad.right - 6.0f, pad.top + 34.0f };
        cv.FillRoundRect(m_closeRect, 5.0f, WithAlpha(pal.vermilion, 0.18f));
        TextStyle cs; cs.role = FontRole::Sans; cs.size = 16.0f; cs.hAlign = HAlign::Center; cs.vAlign = VAlign::Middle;
        cv.Text(L"×", m_closeRect, cs, pal.vermilion);
    }
}

} // namespace lj
