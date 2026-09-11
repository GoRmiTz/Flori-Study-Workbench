# ============================================================
#  shotdiff.ps1 — 截图基线比对（S2-3 防回归）
#
#  为什么要它：`--shot` 只验 exit=0，等于「没崩就算过」。改一行布局把
#  文字压到卡片外、把按钮挤重叠，退出码照样是 0。本脚本把「页面长什么样」
#  也纳入回归网——与基线逐像素比，差异超阈值就红。
#
#  依赖：只用 .NET 自带 System.Drawing（Windows PowerShell 5.1 即可），
#        不装任何第三方包，符合项目「零依赖」约定。
#
#  用法：
#    生成/更新基线（确认当前效果正确后再跑）：
#      powershell -ExecutionPolicy Bypass -File tools\shotdiff.ps1 -Mode baseline
#    比对（CI 用）：
#      powershell -ExecutionPolicy Bypass -File tools\shotdiff.ps1 -Mode check
#
#  退出码：0 = 全部在阈值内；1 = 有页面超阈值；2 = 前置缺失（无基线/无截图）
# ============================================================
param(
    [ValidateSet("baseline", "check")]
    [string]$Mode = "check",

    # 当前截图目录（相对仓库根 DesktopApp/）
    [string]$ShotDir = "build\shots\ci",

    # 基线目录
    [string]$BaselineDir = "tools\baseline",

    # 允许的差异像素占比（0.005 = 0.5%）。反锯齿/时间相关元素会有微小抖动，
    # 阈值太严会天天误报；0.5% 足以拦住「整块布局位移」这类真回归。
    [double]$Threshold = 0.005,

    # 单像素被判定为「不同」的通道差阈值，压掉编码噪声
    [int]$ChannelTolerance = 8
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$shotPath = Join-Path $root $ShotDir
$basePath = Join-Path $root $BaselineDir

if (-not (Test-Path $shotPath)) {
    Write-Host "[diff] 截图目录不存在：$shotPath（先跑截图自检）"
    exit 2
}

$shots = @(Get-ChildItem -Path $shotPath -Filter *.png -ErrorAction SilentlyContinue)
if ($shots.Count -eq 0) {
    Write-Host "[diff] $shotPath 下没有 png（无头环境跳过截图？）"
    exit 2
}

# ---------- baseline 模式：把当前截图整体固化为基线 ----------
if ($Mode -eq "baseline") {
    New-Item -ItemType Directory -Force -Path $basePath | Out-Null
    foreach ($s in $shots) {
        Copy-Item $s.FullName (Join-Path $basePath $s.Name) -Force
    }
    Write-Host "[diff] 已更新基线 $($shots.Count) 张 → $basePath"
    exit 0
}

# ---------- check 模式 ----------
if (-not (Test-Path $basePath)) {
    Write-Host "[diff] 基线目录不存在：$basePath"
    Write-Host "[diff] 首次使用请先确认当前渲染正确，然后运行： tools\shotdiff.ps1 -Mode baseline"
    exit 2
}

# 逐像素比较两张图，返回差异像素占比；尺寸不同直接算 1.0（100% 差异）
function Compare-Png([string]$a, [string]$b, [int]$tol) {
    $ia = [System.Drawing.Bitmap]::FromFile($a)
    $ib = [System.Drawing.Bitmap]::FromFile($b)
    try {
        if ($ia.Width -ne $ib.Width -or $ia.Height -ne $ib.Height) { return 1.0 }

        $rect = New-Object System.Drawing.Rectangle 0, 0, $ia.Width, $ia.Height
        $fmt = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $da = $ia.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $fmt)
        $db = $ib.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $fmt)
        try {
            $count = $ia.Width * $ia.Height * 4
            $ba = New-Object byte[] $count
            $bb = New-Object byte[] $count
            [System.Runtime.InteropServices.Marshal]::Copy($da.Scan0, $ba, 0, $count)
            [System.Runtime.InteropServices.Marshal]::Copy($db.Scan0, $bb, 0, $count)

            $diff = 0
            for ($i = 0; $i -lt $count; $i += 4) {
                if ([Math]::Abs($ba[$i]     - $bb[$i])     -gt $tol -or
                    [Math]::Abs($ba[$i + 1] - $bb[$i + 1]) -gt $tol -or
                    [Math]::Abs($ba[$i + 2] - $bb[$i + 2]) -gt $tol) { $diff++ }
            }
            return $diff / ($ia.Width * $ia.Height)
        } finally {
            $ia.UnlockBits($da); $ib.UnlockBits($db)
        }
    } finally {
        $ia.Dispose(); $ib.Dispose()
    }
}

$bad = 0
$missing = 0
foreach ($s in $shots) {
    $bl = Join-Path $basePath $s.Name
    if (-not (Test-Path $bl)) {
        Write-Host ("  --   {0,-12} 无基线（新页面？跑 -Mode baseline 收编）" -f $s.BaseName)
        $missing++
        continue
    }
    $ratio = Compare-Png $s.FullName $bl $ChannelTolerance
    if ($ratio -gt $Threshold) {
        Write-Host ("  FAIL {0,-12} 差异 {1:P2} > 阈值 {2:P2}" -f $s.BaseName, $ratio, $Threshold)
        $bad++
    } else {
        Write-Host ("  ok   {0,-12} 差异 {1:P2}" -f $s.BaseName, $ratio)
    }
}

Write-Host ""
if ($bad -gt 0) {
    Write-Host "[diff] $bad 个页面视觉回归；确认是有意改动就跑： tools\shotdiff.ps1 -Mode baseline"
    exit 1
}
Write-Host "[diff] 视觉基线通过（$($shots.Count - $missing) 张比对，$missing 张待收编）"
exit 0
