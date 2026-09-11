#!/usr/bin/env bash
# ============================================================
#  msvcenv.sh — 在 Git Bash 里手工装配 MSVC 环境
#  等价于 vcvars64.bat，但不依赖 cmd.exe，便于脚本化构建。
#  用法： source tools/msvcenv.sh
# ============================================================

# 这个环境没有 cygpath，自己做两向路径转换
_w2u() { echo "$1" | sed -e 's|\\|/|g' -e 's|^\([A-Za-z]\):|/\L\1|'; }
_u2w() { echo "$1" | sed -e 's|^/\([A-Za-z]\)/|\U\1\E:/|'; }

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
[ -f "$VSWHERE" ] || VSWHERE="/c/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"

VSPATH_WIN="$("$VSWHERE" -latest -products '*' \
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
  -property installationPath 2>/dev/null | tr -d '\r')"

if [ -z "$VSPATH_WIN" ]; then
  echo "[X] 未找到含 C++ 工具集的 Visual Studio" >&2
  return 1 2>/dev/null || exit 1
fi

VS="$(_w2u "$VSPATH_WIN")"
MSVC_VER="$(ls "$VS/VC/Tools/MSVC" | sort -V | tail -1)"
VC="$VS/VC/Tools/MSVC/$MSVC_VER"

KITS_WIN="C:/Program Files (x86)/Windows Kits/10"
KITS="/c/Program Files (x86)/Windows Kits/10"
SDK_VER="$(ls "$KITS/Include" | sort -V | tail -1)"

VC_WIN="$(_u2w "$VC")"

export INCLUDE="$VC_WIN/include;$KITS_WIN/Include/$SDK_VER/ucrt;$KITS_WIN/Include/$SDK_VER/um;$KITS_WIN/Include/$SDK_VER/shared;$KITS_WIN/Include/$SDK_VER/winrt;$KITS_WIN/Include/$SDK_VER/cppwinrt"
export LIB="$VC_WIN/lib/x64;$KITS_WIN/Lib/$SDK_VER/ucrt/x64;$KITS_WIN/Lib/$SDK_VER/um/x64"
export LIBPATH="$LIB"

export PATH="$VC/bin/Hostx64/x64:$KITS/bin/$SDK_VER/x64:$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:$PATH"

# cmake.exe / ninja.exe 是 Windows 程序，参数路径必须用 Windows 风格（C:/...），
# 不能用 POSIX 风格（/c/...），否则报 "no such file or directory" 且写坏 build 缓存。
VS_WIN="$(printf '%s' "$VSPATH_WIN" | sed 's|\\|/|g')"
export LJ_CMAKE="$VS_WIN/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
export LJ_NINJA="$VS_WIN/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"

echo "[i] MSVC $MSVC_VER / SDK $SDK_VER 就绪"
