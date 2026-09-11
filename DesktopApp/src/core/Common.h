#pragma once
// ============================================================
//  Common.h — 全局基础设施
// ============================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdint>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace lj {

// ---------- 日志：同时输出到调试器与 exe 旁的 flori.log ----------
void LogInit();
void LogLine(const wchar_t* fmt, ...);
void LogHR(const wchar_t* what, HRESULT hr);

// ---------- 路径 ----------
std::wstring ExeDir();
// 资源查找：优先 exe 旁 assets/，回落到源码目录 assets/（便于开发期热改）
std::wstring ResolveAsset(const std::wstring& relative);
bool ReadTextFile(const std::wstring& path, std::string& out);
uint64_t FileWriteTime(const std::wstring& path);

// ---------- 常用数学 ----------
inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace lj

// 失败即记录并返回
#define LJ_HR(expr)                                          \
    do {                                                     \
        HRESULT _hr__ = (expr);                              \
        if (FAILED(_hr__)) {                                 \
            ::lj::LogHR(L#expr, _hr__);                      \
            return _hr__;                                    \
        }                                                    \
    } while (0)

#define LJ_HR_BOOL(expr)                                     \
    do {                                                     \
        HRESULT _hr__ = (expr);                              \
        if (FAILED(_hr__)) {                                 \
            ::lj::LogHR(L#expr, _hr__);                      \
            return false;                                    \
        }                                                    \
    } while (0)
