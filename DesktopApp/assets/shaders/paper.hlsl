// ============================================================
//
//
//

cbuffer FrameCB : register(b0)
{
    float2 uResolution;   // 鍚庡彴缂撳啿鍍忕礌灏哄
    float  uTime;
    float  uScroll;       // 褰撳墠婊氬姩浣嶇Щ锛堝儚绱狅級

    float4 uPaper;        // 绾稿熀浜壊
    float4 uPaperLo;      // 绾稿熀鏆楄壊锛堣竟缂橈級
    float4 uGridColor;    // 缃戞牸绾胯壊锛宎 涓哄己搴?
    float  uGridUnit;
    float  uGridMajor;
    float  uScale;        // DPI 缂╂斁
    float  uVignette;     // 鏆楄寮哄害

    float  uGrain;        // 绾ょ淮鍣偣寮哄害
    float  uParallax;     // 缃戞牸瑙嗗樊绯绘暟
    float  uReveal;       // 鍏ㄥ眬鏄惧奖杩涘害 0..1锛堝姞杞藉睆鐢級
    float  uPad0;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

//
float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float vnoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p)
{
    float v = 0.0, amp = 0.5;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        v += amp * vnoise(p);
        p *= 2.03;
        amp *= 0.5;
    }
    return v;
}

    float gridLine(float2 px, float spacing, float thickness)
{
    float2 g = px / spacing;
    float2 d = abs(frac(g - 0.5) - 0.5) / fwidth(g);
    float l = min(d.x, d.y);
    return 1.0 - smoothstep(0.0, thickness, l);
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float2 px = i.uv * uResolution;
    float2 c  = i.uv - 0.5;

    //
    float radial = saturate(length(c * float2(1.0, 1.15)) * 1.35);
    float3 col = lerp(uPaper.rgb, uPaperLo.rgb, radial * 0.85);

    //
    float2 np = px / max(uScale, 0.5);
        float fibre = fbm(np * 0.9) - 0.5;
    float speck = hash21(floor(np * 1.7)) - 0.5;
    float streak = vnoise(float2(np.x * 0.05, np.y * 2.2)) - 0.5;
    col += (fibre * 0.55 + speck * 0.35 + streak * 0.30) * uGrain;

    //
    float2 gpx = px + float2(0.0, -uScroll * uParallax);
    float unitPx  = uGridUnit  * uScale;
    float majorPx = uGridMajor * uScale;

    float fine  = gridLine(gpx, unitPx,  1.0);
    float major = gridLine(gpx, majorPx, 1.35);

    col = lerp(col, uGridColor.rgb, fine  * uGridColor.a * 0.55);
    col = lerp(col, uGridColor.rgb, major * uGridColor.a * 1.25);

    //
    float2 gm = frac(gpx / majorPx) - 0.5;
    float2 dm = abs(gm) * majorPx;
    float tickLen = 3.0 * uScale;
    float tick = (1.0 - smoothstep(0.0, 1.2, dm.x)) * step(dm.y, tickLen)
               + (1.0 - smoothstep(0.0, 1.2, dm.y)) * step(dm.x, tickLen);
    col = lerp(col, uGridColor.rgb, saturate(tick) * uGridColor.a * 1.6);

    //
    float breathe = 0.006 * sin(uTime * 0.35 + i.uv.x * 1.7);
    col += breathe;

    //
    float vig = 1.0 - uVignette * pow(radial, 2.2);
    col *= vig;

    //
    if (uReveal < 0.999)
    {
        float edge = smoothstep(uReveal - 0.28, uReveal + 0.02, radial);
        col = lerp(col, uPaperLo.rgb * 0.94, edge);
    }

    return float4(col, 1.0);
}
