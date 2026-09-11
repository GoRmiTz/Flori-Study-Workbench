# 芙洛理 Flori · Microsoft Store 打包说明

本目录是「P2-3 方案 A」里 **Microsoft Store（MSIX）打包脚手架** 的占位与说明。
桌面端是原生 Win32（D3D11/D2D1，零第三方依赖），上架 Store 走 **Desktop Bridge /
Windows Application Packaging Project**，把 `Flori.exe` 包成 MSIX 上传合作伙伴中心。

## 1. 资源占位（assets/）

`appxmanifest.xml` 引用了以下图片，打包前需放入 `Store/assets/`：

| 文件 | 尺寸 | 用途 |
|------|------|------|
| `StoreLogo.png` | 50×50 | 商店列表图标 |
| `Square44x44Logo.png` | 44×44 | 开始菜单小图 |
| `Square150x150Logo.png` | 150×150 | 开始菜单磁贴 |
| `Wide310x150Logo.png` | 310×150 | 宽磁贴 |
| `SplashScreen.png` | 620×300 | 启动闪屏（Store 包装层；应用内另有自绘 Loader） |

建议用品牌朱砂色（`#6B2A35`）为底，白色「律」字章为主视觉。

## 2. 三种打包方式（任选其一）

### A. Visual Studio · Windows Application Packaging Project（推荐，最省心）
1. 在解决方案里「添加新项目 → Windows 应用程序打包项目」，命名 `Flori.Package`。
2. 右键「应用程序 → 引用」添加对桌面端项目（`Flori`）的引用。
3. 把 `Store/assets/` 设为包项目资产，「Package.appxmanifest」可**直接复用本目录
   `appxmanifest.xml`**（改扩展名或合并字段），填写发布者标识与版本。
4. 右键包项目 → **发布 → 创建应用程序包 → 旁加载/商店**，产出 `.msixupload`。

### B. 命令行 makeappx（CI 友好）
```bat
:: 准备布局目录 layout\ 放 Flori.exe + assets\
makeappx pack /d layout /p Flori.msix /o
:: 用商店证书签名（需先有 .pfx）
signtool sign /fd SHA256 /a /f flori.pfx Flori.msix
```

### C. msix 工具（开源 CLI）
```bat
msix pack -d layout -p Flori.msix
```

## 3. 与自动更新的关系

- Store 上架后，**版本更新由合作伙伴中心分发**，不再走客户端 `Updater`。
- `Updater`（拉 `Server /version`、后台下载、重启应用）面向**直装 / 官网分发**场景。
- 上架 Store 时，建议在 `Updater::CheckAsync` 前加一个开关（如读取 `AppxManifest`
  发布者判断在 Store 包内则跳过），避免与商店更新机制打架。当前实现已用
  `IsDevPath()` 保护开发/截图路径，Store 包内的开关留作后续收口项。

## 4. 收尾清单（上线前）

- [ ] 采购代码签名证书（EV 证书可免 SmartScreen 拦截；普通 OV 也可，首次信誉需累积）
- [ ] 申请合作伙伴中心开发者账号，拿到发布者标识与 `{IdentityName}`
- [ ] 准备 `assets/` 全套图标与闪屏
- [ ] 在 `Updater` 加「Store 包内禁用自更新」开关
- [ ] 真机跑一遍：安装 → 启动 → 托盘「检查更新」→（发版后）「重启并更新」自动替换
