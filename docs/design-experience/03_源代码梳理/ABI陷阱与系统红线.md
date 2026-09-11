# 03-C · ABI 陷阱与系统红线

> 来源：`协作与交接手册.md` §6、`任务清单.md`、working memory 铁律。这些是从真实踩坑中沉淀的**必读陷阱**，芙洛理沿用 C++ 核心时务必遵守。

## 1. ABI 陷阱（最致命）
- 改影响 `sizeof(App)` 的头（典型 `TopBar.h`/`App.h`/任何 App 直接持有的视图/成员头）→ **必须** `touch src/main.cpp` 再构建，否则启动段错误 139。
- `lj::App app;` 是栈分配，大小取决于所有被它持有的成员头。

## 2. 窗口创建铁律（曾因漏写导致建窗失败 1400）
- 自定义 `WndProcStatic` 在 `WM_NCCREATE` **必须同时** `SetWindowLongPtrW(GWLP_USERDATA, self)` 与 `self->m_hwnd = hwnd`。
- 漏 `m_hwnd=hwnd` → 创建期 `DefWindowProcW(nullptr)` → 返回 1400 建窗失败。
- `Window.cpp` 为可靠模板。

## 3. 渲染约束（swap chain / D2D）
- swap chain **必须 `DXGI_SWAP_EFFECT_SEQUENTIAL`（blit）**，禁 `FLIP*`（隐藏 Win32 EDIT 需 blit 合成）。
- D2D target bitmap **必须 `TARGET | CANNOT_DRAW`**，否则 `CreateBitmapFromDxgiSurface` 返 `E_INVALIDARG` 启动失败。
- 转场**只用 PushTransform 位移 + 页面不透明**；禁整页 `PushLayer(opacity)`、禁离屏位图（CANNOT_DRAW 无法回读，部分机器崩溃）。

## 4. EDIT / 中文 IME
- 主窗口 `WS_CLIPCHILDREN`；**严禁每帧 `RedrawWindow(EDIT)`**（频闪）。
- 中文输入用隐藏 EDIT 代理；`HideCaret` + `EM_SETSEL(len,len)` 防抖；`WM_SETCURSOR` 用 `IsChild` 统一放行子窗口（含 RichEdit）。

## 5. 布局铁律
- 纵向堆叠**必须用 `lj::ui::VLayout`**，禁手算 `y+=h`（S2-1 重叠根因）。
- 拖拽命中测试全程屏幕坐标（GetCursorPos - GetWindowRect），勿混 client/GetWindowRect。

## 6. 零依赖红线
- 系统库白名单见 `CMakeLists.txt`；引入 npm 包/C++ 库需评审。
- Godot 作为外部子进程**不计入**主程序依赖（MIT，可选移除）。

## 7. 看板娘铁律（勿逆）
- 看板娘 Png 皮套无需自建着色器；若未来加动效后端，着色器须内嵌（D3D11 运行时 D3DCompile），勿改回部署 Shaders 目录。
- GL 线程方案已废弃（NVIDIA 离屏崩溃），勿再尝试，用 D3D11。
- SEH(C2712)：含析构对象函数内禁 `__try/__except`；危险调用收口到裸指针/POD 静态函数。
- 缩放/超采样：`SetDisplaySize` 必须同比例重建内部超采样画布（否则下采样越界→重影）。
- `near/far` 是 windows.h 宏，变量名禁用（用 nearModel 等替代）。
- 单行 EDIT 不向父窗口发 EN_RETURN；"回车=发送"须子类化 EDIT 截 WM_CHAR VK_RETURN。

## 8. Git 铁律
- **绝不用 `git rm`**（曾删光 src/）；删文件用 `rm -f` + `git add -A`。
- LNK1168=旧 exe 未退：`taskkill` 后重编。

## 9. 安全红线（S0）
- Updater 仅 `https://` 或回环 `http://`；缺 64 位 sha256 拒下载；下载后内存内校验再落盘；交换前重读磁盘再验。
- 口令 PBKDF2-HMAC-SHA256（60 万/16B 盐/32B）；演示口令收口服务端配置、不落文档。
- API 凭据**本地文件、不入库、云端 payload 不携带**。

## 10. 动效/概念铁律（见 `04` 铁律㉘–㉟）
- 零辉光、克制、信息可视化优先、真实操作驱动、确定可控非游戏、落真实界面。
- **㉟ 拒绝花瓶**：每个动效必须回答"落在软件哪里 + 买点/价值闭环"。
- **㉞ 禁止重复"轨道观察结构"范式**：换母题也要换交互范式（织/编/显影/折叠…）。
- **㉝ 亮色背景**：暖白纸底 + 墨线主导。
