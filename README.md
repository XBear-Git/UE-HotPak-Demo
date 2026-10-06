# HotPakDemo

基于 **Unreal Engine 5.7** 的客户端资源热更新（Pak 补丁）Demo 项目。

核心目标：在不重新安装整包的前提下，通过运行时挂载增量 Pak，实现「改资源 → 生成补丁 → 客户端挂载 → 生效」的完整闭环。

---

## 一、项目简介

本项目实现了一套**传统 Pak 路线的资源热更新框架**：

- **服务端**维护版本清单（`version.json`）与增量 Pak；
- **客户端**启动时拉取清单做差异比对，仅下载变更的增量 Pak，校验后运行时挂载生效；
- **挂载失败自动回滚**到上一稳定版本（基础版）；断网/清单错误/包损坏/挂载失败/磁盘不足等异常全程不崩溃。

当前实现已覆盖**完整闭环 + 健壮性**：检测 → 下载 → SHA-256 校验 → 运行时挂载 → 资源生效 → 持久化版本；并具备**挂载失败回滚、异常全覆盖、C++ Slate 更新面板、关键节点耗时统计**。

### 1.1 系统架构

```
┌────────────────────────── 服务端（本地静态站） ──────────────────────────┐
│  python -m http.server 8000   （PatchServer/ 目录）                       │
│    ├── version.json      版本清单：latest / min_supported / force + pak   │
│    └── patches/*.pak     增量补丁 Pak（UnrealPak 生成）                   │
└──────────────────────────────────┬───────────────────────────────────────┘
                                   │  HTTP GET
                                   ▼
┌────────────────────────── 客户端 UE 运行时 ──────────────────────────────┐
│  UHotUpdateSubsystem（GameInstanceSubsystem）—— 编排 / 生命周期          │
│    ├─ UPakMounter          启动重挂载稳定补丁 / Mount / Unmount / 回滚    │
│    ├─ UVersionManager      拉 version.json、版本比对、稳定/失败记录       │
│    ├─ UUpdateStateMachine  Idle→Checking→DL→Verify→Mount→Done / Rollback │
│    ├─ UPakDownloader       单文件串行下载 → .tmp（进度回调）              │
│    ├─ UPakVerifier         大小 + SHA-256（自带实现）校验                 │
│    ├─ UVersionRecord       SaveGame：本地版本 / 稳定补丁 / 上次失败版本    │
│    └─ SUpdatePanel         C++ Slate 更新面板（右下角：进度/状态/按钮）    │
│                                                                          │
│  挂载成功 → NotifyHotfixApplied → ABaseHotTestActor 重载四类资源槽         │
│  （DataTable / 贴图 / Actor / 关卡）                                     │
└──────────────────────────────────────────────────────────────────────────┘
```

### 1.2 关键技术决策与取舍

| 决策 | 选择 | 原因 / 取舍 |
|---|---|---|
| 容器格式 | **关闭 IoStore / ZenStore，走传统 `.pak`** | 自写 Mount / PakOrder 逻辑清晰可控；IoStore（.utoc/.ucas）机制不可控，不进入本链路 |
| 补丁生成 | **自研 Python 管线（HotPatcher 未适配 UE 5.7）** | 自研能讲清 UnrealPak 文件级 diff 机制。代价：暂无依赖分析 |
| SHA-256 | **自带可移植实现** | UE 在 Windows 无 `FPlatformMisc::GetSHA256Signature` 实现（直接 `checkf(false)`） |
| 下载目录 | **`Saved/HotUpdate`**（非 `Saved/Paks`） | `Saved/Paks` 会被 `GetPakFolders()` 当扫描目录自动挂载、占用文件句柄 |
| 落地命名 | **`Patch_<版本>_<sha8>.pak`** | 避免同名覆盖冲突，旧/新补丁可共存，回滚更稳 |
| 挂载点 | **`Mount(..., nullptr)`** | 用 Pak 自带挂载点自动对齐，避免手写 MountPoint 出错 |
| 更新 UI | **C++ Slate（非 UMG）** | 不依赖任何 UMG 资产 → 冻结的基础包无需重建；纯 C++ 也契合 C++ 岗位 |
| 回滚策略 | **目标固定为基础版**（本 Demo 单补丁） | 简单可靠；多跳升级需保留上一稳定补丁的 order 策略，属扩展 |
| 重测 | **手动清客户端状态** | 简化实现；正式产品应有增量清理 / 版本回退策略 |

---

## 二、已实现功能

| 功能 | 说明 |
|---|---|
| ✅ 关闭 IoStore，走传统 Pak | `DefaultGame.ini` 中 `bUseIoStore=False` / `bUseZenStore=False` / `bUsePakFile=True`，是热更新的前提 |
| ✅ 运行时 Pak 挂载封装 | `UPakMounter` 封装 `FPakPlatformFile::Mount` / `Unmount`，补丁以 `order=100` 覆盖基础包；控制台 `HotUpdate.MountPak` 仍可用 |
| ✅ 热更新验证框架 | 子系统（状态/回调）+ 测试 Actor（四类资源槽）+ DataTable 行结构，三层解耦 |
| ✅ 自研补丁生成管线 | Python 脚本：导出基线 → SHA256 diff → UnrealPak 打增量 Pak |
| ✅ 服务端版本清单 | `PatchServer/version.json`（版本号、URL、大小、SHA256、挂载点） |
| ✅ 远端版本清单拉取与比对 | `UVersionManager` 用 `FHttpModule` 异步 GET `version.json`，按**数字分段**比较版本，输出「需更新 / 已最新 / 强制更新」，并解析 `pak` 下载信息 |
| ✅ 更新流程状态机 | `UUpdateStateMachine` 定义 `EHotUpdateState`（`Idle/Checking/UpToDate/NeedUpdate/ForceUpdate/Downloading/Verifying/Mounting/Done/Failed/RollingBack` 全部启用），每次转移打印日志并广播 |
| ✅ 本地版本持久化（FR-08） | 挂载成功后写入稳定版本 + 稳定补丁文件名（`UVersionManager::SaveStableInfo` → `UVersionRecord`，槽 `HotUpdateVersion`），作为下次启动比对基线 |
| ✅ 增量 Pak 下载 | `UPakDownloader` 基于 `FHttpModule` 单文件串行下载，先落 `Saved/HotUpdate/*.pak.tmp`，带进度回调（不做断点续传/并发） |
| ✅ SHA-256 + 大小校验 | `UPakVerifier` + **自带的可移植 SHA-256**（UE 在 Windows 无可用实现），先比大小再比哈希，通过后 `.tmp` 改名 `.pak` |
| ✅ 下载失败重试 | 「下载 + 校验」为一个尝试单元，最多 3 次；`ForceUpdate` 检测完自动下载，`NeedUpdate` 等 `HotUpdate.ConfirmUpdate` 确认 |
| ✅ 挂载 → 资源生效闭环 | 校验通过后 `Mounting → Done`，并 `NotifyHotfixApplied` 让测试场景重载资源（数据表/贴图/材质/关卡等） |
| ✅ 启动重挂载 | `RemountStablePatch()` 在关卡加载前**只挂载记录的稳定补丁**（避免多补丁 order 冲突）；缺失/损坏则回落基础版并重置版本 |
| ✅ 挂载失败回滚（FR-09） | 任何终态失败走 `Failed → RollingBack → Done`；Mount 失败做**实质回滚**（卸载坏包 + 删包 + 重置稳定版为基础版），并记录 `LastFailedVersion` |
| ✅ 异常处理全覆盖 | 断网 / HTTP 非 200、清单解析失败、Pak 损坏、Mount 失败、**磁盘空间不足**（下载前预检）均有区分日志与状态机统一处理，全程不崩溃 |
| ✅ 唯一落地名 | 下载落地为 `Patch_<版本>_<sha8>.pak`，同版本重下不再与已挂载文件冲突，旧 / 新补丁可共存 |
| ✅ ForceUpdate 防死循环 | 同一版本上次更新失败则不再自动下载，提示手动重试；远端版本变化后恢复 |
| ✅ 更新流程 UI（FR-10） | C++ Slate 面板 `SUpdatePanel`（右下角）：版本 / 状态文案、下载进度条 + 剩余大小、「更新 / 重试 / 跳过」按钮；面板显示时自动开鼠标（`GameAndUI`） |
| ✅ 耗时统计 | 补丁体积、下载 / 校验 / 挂载 / 总耗时、尝试次数，统一 `[HotUpdate][Stats]` 日志 |
| ✅ 本地 HTTP 静态服务 | `python -m http.server 8000` 把 `PatchServer/` 当静态站点，模拟服务端下发清单与 Pak |

---

## 三、环境与版本锁定

| 项 | 值 |
|---|---|
| 引擎版本 | **UE 5.7.4**（Changelist 51494982，源码版） |
| 引擎路径 | `D:\UE\UE_5.7` |
| 目标平台 | Windows 64 |
| 脚本运行时 | Python 3.12+ |
| 打包工具 | UnrealPak（`Engine\Binaries\Win64\UnrealPak.exe`） |

> ⚠️ Cook、Pak、Mount 三者必须同一引擎版本与项目设置，否则报 `version mismatch`。

---

## 四、目录结构

```
HotPakDemo/
├── Source/HotPakDemo/               C++ 运行时模块
│   ├── Core/                        核心逻辑（挂载/回滚、状态机、版本管理/持久化、下载/校验、SHA-256、Slate 面板、测试 Actor）
│   ├── Structs/                     DataTable 行结构
│   └── HotPakDemo.Build.cs          模块依赖
├── Content/HotPatchDemo/            热更测试资源（按类型分目录）
│   ├── Core/ Textures/ Materials/ DataTables/ Meshes/ Blueprints/ Maps/ UI/
├── Config/DefaultGame.ini           打包设置（关闭 IoStore 等）
├── BuildScripts/                    自研补丁生成脚本（Python）
├── PatchServer/                     模拟服务端
│   ├── manifests/                   基线清单 Release_*.json
│   ├── patches/                     增量 Pak 及响应文件
│   ├── full/                        完整包（预留）
│   └── version.json                 服务端版本清单
└── Tools/                           第三方工具（预留）
```

---

## 五、关键代码文件

### C++（`Source/HotPakDemo/`）

| 文件 | 职责 |
|---|---|
| `Core/HotUpdateSubsystem.h/.cpp` | **核心/编排**。`UGameInstanceSubsystem`：创建状态机/版本管理器/挂载器，先 `RemountStablePatch()` 再检测；`ForceUpdate` 自动下载（带防死循环守卫）、`NeedUpdate` 待确认；校验通过后 `MountDownloadedPak()`（挂载 → 通知生效 → `SaveStableInfo` → `Done`）；失败走 `Rollback()` + 磁盘空间预检 + 耗时统计 + Slate 面板生命周期（含鼠标/输入模式）。控制台命令 `HotUpdate.MountPak` / `CheckForUpdate` / `Status` / `ConfirmUpdate` / `RetryUpdate` / `SelfTest` |
| `Core/UpdateStateMachine.h/.cpp` | 更新状态机。`EHotUpdateState`（`Idle/Checking/UpToDate/NeedUpdate/ForceUpdate/Downloading/Verifying/Mounting/Done/Failed/RollingBack` 全部启用）；`TransitionTo()` 打印日志并广播 `OnStateChanged` |
| `Core/VersionManager.h/.cpp` | 版本检测与比对。`FHttpModule` 异步 GET `version.json`，解析 `latest_version`/`min_supported_client`/`force_update` 与 `pak`；`CompareVersion()` 按数字分段比较；`SaveStableInfo()` / `SaveLastFailedVersion()` / `GetStablePakFileName()` / `GetLastFailedVersion()`；检测时不写盘 |
| `Core/VersionRecord.h/.cpp` | 本地记录（`USaveGame`，槽 `HotUpdateVersion`）：`LocalVersion` + `StablePakFileName` + `LastFailedVersion`；`GetBaseVersion()` 具名常量、`LoadOrCreateRecord()`、`SaveStableInfo()`（成功写稳定版+清失败记录）、`SaveLastFailedVersion()` |
| `Core/PakDownloadInfo.h` | `USTRUCT FPakDownloadInfo`：`Url/Size/Sha256/Order/MountPoint`，对应 `version.json` 的 `pak` 字段，供下载/校验/挂载共用 |
| `Core/PakDownloader.h/.cpp` | `UPakDownloader`：`FHttpModule` 单文件串行下载；`OnRequestProgress64` 进度回调；完成后把响应体写入目标 `.tmp`；委托 `OnProgress`/`OnComplete`（不做断点续传/并发） |
| `Core/PakVerifier.h/.cpp` | `UPakVerifier`：`EPakVerifyResult`（Success/FileNotFound/SizeMismatch/HashMismatch/ReadError）；**先比大小再算 SHA-256**，大小写不敏感 |
| `Core/PakMounter.h/.cpp` | `UPakMounter`：挂载/卸载封装（`MountPak(Path, Order)` / `UnmountPak` / `GetMountedPakFilenames` / `static GetLocalPatchDirectory`）。**只挂载**，不广播、不写版本（通知与持久化由子系统负责） |
| `Core/SHA256.h/.cpp` | 自带可移植 SHA-256（`HashBytes` / `HashFile` 分块读 / `RunSelfTest`）。UE 的 `FPlatformMisc::GetSHA256Signature` 在 Windows 无实现，故自研 |
| `Core/HotUpdateTypes.h` | `EHotUpdateError`（Network/ManifestParse/SizeMismatch/HashMismatch/DiskSpace/FileIO/MountFailed/RollbackFailed）、`FHotUpdateStats`（体积 / 各步骤耗时 / 尝试次数） |
| `Core/UpdatePanel.h/.cpp` | `SUpdatePanel`（C++ Slate，`SCompoundWidget`）：右下角更新面板，版本/状态/进度/剩余大小 + 更新/重试/跳过按钮；纯代码，不依赖 UMG 资产 |
| `Core/BaseHotTestActor.h/.cpp` | 热更验证场景的执行器。持有四类软引用资源槽（贴图/数据表/Actor/关卡），观察子系统回调，提供 `ForceReloadAllSlots()` 手动刷新入口 |
| `Structs/ST_HotfixConfig.h` | `FTableRowBase` 子类，DataTable 行结构（版本号、倍率、生命、标题、开关、颜色等），用于验证数据热更 |
| `HotPakDemo.cpp/.h` | 主模块 + `LogHotUpdate` 日志分类 |

### 关键实现说明

- **挂载点对齐**：`MountPak` 调用 `FPakPlatformFile::Mount(pakPath, order, nullptr)`，传 `nullptr` 让引擎使用 Pak 自带的挂载点，自动对齐（避免手写 MountPoint 出错）。
- **覆盖优先级**：增量 Pak 以 `PakOrder = 100` 挂载，高于基础包（order 0），资源查找时补丁优先。
- **可观测性**：挂载成功/失败用 `AddOnScreenDebugMessage` 在屏幕显示（绿/红），同时 `UE_LOG(LogHotUpdate, ...)` 记录到日志文件。
- **异步安全**：HTTP 回调通过 `BindUObject`（弱引用）绑定，对象销毁后不会悬空调用。
- **下载目录**：补丁下载到 `Saved/HotUpdate/`，**不能放 `Saved/Paks`**——引擎的 `GetPakFolders()` 会把 `Saved/Paks` 也当 pak 扫描目录（`ALL_PAKS_WILDCARD="*.pak"`），放进去会被自动挂载并占用文件句柄，导致改名覆盖失败。
- **SHA-256 自研**：UE 5.7 的 `FPlatformMisc::GetSHA256Signature` 在 Windows 平台没有实现（`GenericPlatformMisc.cpp` 里直接 `checkf(false)`），因此项目内置一份可移植 SHA-256，与 Python `hashlib.sha256().hexdigest()` 完全一致。
- **启动重挂载（只挂稳定补丁）**：`RemountStablePatch()` 在关卡加载前挂载 `UVersionRecord::StablePakFileName` 记录的补丁；挂载成功后调用 `NotifyHotfixApplied(本地版本)`，使 `ABaseHotTestActor::BeginPlay` 触发 `OnHotfixApplied` 刷新 UI/数值。补丁缺失 / 损坏则回落基础版并重置版本。
- **挂载失败回滚（FR-09）**：任何终态失败统一走 `Failed → RollingBack → Done`；只有 Mount 失败做**实质回滚**（`Unmount` + 删坏包 + 重置稳定版为基础版 + 记 `LastFailedVersion`），其余失败只清理残留（no-op）。回滚**不调用** `NotifyHotfixApplied`（坏包没挂上，避免把 `bPatchMounted` 误置 true），改用 `OnUpdateRolledBack` 通知 UI。
- **唯一落地名 + 防死循环**：落地为 `Patch_<版本>_<sha8>.pak`，避免与本地已挂载的同名补丁冲突；`ForceUpdate` 遇到 `RemoteVersion == LastFailedVersion` 时不再自动下载，提示手动重试。
- **Slate 更新面板**：纯 C++ `SUpdatePanel`（不依赖 UMG 资产 → 基础包保持冻结），位于右下角；面板可见时 `SetShowMouseCursor(true)` + `GameAndUI`，隐藏时恢复 `GameOnly`（游戏默认不显示鼠标，靠这里打开）。
- **挂载 ≠ 替换内存对象**：Pak 挂载只改变文件系统层解析，已在内存中加载的 `UObject` 不会被替换；所以材质/贴图通常需**重启**（启动重挂载发生在关卡加载前）才可见。
- **改被 Cook 引用的 C++ 类结构需重 Cook 基础包**：`ABaseHotTestActor` 等被 Blueprint/关卡引用的类增删成员后，旧基础包与新二进制序列化不匹配，需重新 Cook/打包基础包；而新增且未被 Cook 引用的类（如 `UPakMounter`）不受影响。

---

## 六、补丁生成脚本（`BuildScripts/`）

| 脚本 | 作用 |
|---|---|
| `hotpatch_config.py` | 共享路径配置（引擎路径、Cook 目录、挂载点前缀、PatchServer 输出目录） |
| `export_release.py` | 导出基础包基线清单 `Release_<版本>.json`（记录每个资源的 SHA256 + 大小） |
| `gen_patch.py` | 读取基线 → SHA256 diff 找变更文件 → 生成响应文件 → 调 UnrealPak 打增量 Pak → 生成 `version.json` |
| `verify_patch.py` | 用 `UnrealPak -List` 校验补丁内容：变更集 vs 响应文件 vs Pak 实际，报告缺失 / 多余 / 大小（FR-11） |
| `serve.ps1` | 一键启动本地 HTTP 静态服务（`PatchServer/`，默认 8000 端口）；根目录另有双击即用的 `start_server.bat` |
| `README.md` | 补丁生成 SOP（详细步骤） |

**核心原理**：Cook 产物文件级 SHA256 diff——基线打包后记录资源哈希，改资源重新 Cook 后对比哈希找出变更文件，用 UnrealPak 打成增量 Pak（挂载点 `../../../HotPakDemo/Content/`）。

---

## 七、构建与运行

### 一键演示（快速开始）

```powershell
# 1) 启动本地 HTTP 静态服务（提供 version.json 与补丁 Pak）
#    双击项目根目录 start_server.bat，或：
powershell -ExecutionPolicy Bypass -File BuildScripts\serve.ps1
# 验证：浏览器打开 http://127.0.0.1:8000/version.json 能取到 JSON

# 2)（如需重新出补丁）改资源 → 重新 Cook → 生成增量 Pak + 清单
cd BuildScripts
python gen_patch.py 1.0.0 1.1.0
python verify_patch.py 1.0.0 1.1.0    # 校验补丁内容完整性（FR-11）
```

3. 启动打包版 `D:\UE\UE_ExportExe\Windows\HotPakDemo\...\HotPakDemo.exe`：
   - 右下角出现更新面板 → 点「更新」（`NeedUpdate`），或 `ForceUpdate` 自动下载；
   - 日志走 `Downloading → Verifying → Mounting → Done`。
4. 重启客户端验证重启一致性；要重测时先清客户端状态（见下文各验证小节）。

### 编译

```powershell
# Game 目标（打包版运行）
D:\UE\UE_5.7\Engine\Build\BatchFiles\Build.bat HotPakDemo Win64 Development <项目路径>\HotPakDemo.uproject -waitmutex

# Editor 目标（编辑器/蓝图需看到新 C++ 成员时用；须先关闭编辑器）
D:\UE\UE_5.7\Engine\Build\BatchFiles\Build.bat HotPakDemoEditor Win64 Development <项目路径>\HotPakDemo.uproject -waitmutex
```

> ⚠️ 二者不同：编辑器加载的是 Editor 目标（`UnrealEditor-HotPakDemo.dll`），打包版用 `HotPakDemo.exe`。改了蓝图可见的 C++ 成员后，只编 Game 目标编辑器里看不到变化。

### 打包基础包

编辑器 `File > Package Project > Windows`，产出 `pakchunk0-Windows.pak`（基础包，Chunk 0）。

### 生成增量补丁

```powershell
cd BuildScripts
python export_release.py 1.0.0     # 一次性导出基线
# ... 修改资源并重新 Cook ...
python gen_patch.py 1.0.0 1.1.0    # 生成增量 Pak + version.json
```

### 运行时挂载验证

运行打包版游戏，按 `~` 打开控制台：

```
HotUpdate.MountPak D:\UE\UnrealProjects\HotPakDemo\PatchServer\patches\Patch_1.0.0_to_1.1.0.pak
```

屏幕出现绿色「挂载成功」即生效。

### 启动本地 HTTP 服务（版本检测用）

```powershell
cd PatchServer
python -m http.server 8000
```

浏览器或 `Invoke-WebRequest http://127.0.0.1:8000/version.json` 能取到 JSON 即正常。

### 版本检测验证

打包版游戏按 `~` 打开控制台：

```
HotUpdate.CheckForUpdate   # 重新拉取 version.json 并与本地版本比对
HotUpdate.Status           # 打印「本地版本 / 远端版本 / 当前状态」
```

默认本地版本为 `1.0.0`（存档不存在时回落默认值）。只改 `PatchServer/version.json` 的字段即可复现三分支：

| 分支 | `latest_version` | `min_supported_client` | 本地 `1.0.0` 时的结果 |
|---|---|---|---|
| 需更新 | `1.1.0` | `1.0.0` | `Checking → NeedUpdate` |
| 强制更新 | `1.1.0` | `1.1.0` | `Checking → ForceUpdate` |
| 已最新 | `1.0.0` | `1.0.0` | `Checking → UpToDate` |

> 注：本地版本的**唯一写入点**是「挂载成功后」，检测时（含 `UpToDate`）不写盘。

本地版本存档位置：`<打包目录>\Saved\SaveGames\HotUpdateVersion.sav`，删除它即可重置为默认 `1.0.0`。

### 下载与校验验证

`version.json` 的 `min_supported_client` / `force_update` 决定是否需用户确认：

- **ForceUpdate**（本地 < `min_supported_client`）→ 检测完**自动下载**；
- **NeedUpdate** → 停在等待确认，控制台执行 `HotUpdate.ConfirmUpdate` 才开始下载。

流程日志为 `Downloading → Verifying → Mounting → Done`，落盘到 `Saved\HotUpdate\Patch_<版本>_<sha8>.pak`（先写 `.tmp`，校验通过后改名；「下载+校验」失败自动重试，最多 3 次）。

- `HotUpdate.ConfirmUpdate`：确认下载（NeedUpdate 分支）。
- `HotUpdate.SelfTest`：运行 SHA-256 已知向量自测（启动时也会自动跑一次）。
- 制造损坏：改 `version.json` 的 `sha256`（或截断 Pak）→ 应看到「校验失败 → 重试 3 次 → `Failed`」。

### 挂载与重启一致性验证

校验通过后会自动挂载（`Mounting → Done`）并写入本地版本，无需手动命令。重启客户端验证一致性：

- 期望日志：`启动重挂载完成：1/1`、`hot-update startup state, version=1.1.0, patch_mounted=true`、`已最新`。
- 资源表现：数据表 / 贴图 / 材质均为新版（贴图、材质因内存缓存，通常**重启后**才可见）。

**材质 / 贴图热更改法（原地改、保持同名同路径）**：
- 材质：直接打开 `M_HotfixTexture` 改节点/参数并保存；
- 贴图：对 `T_HotfixTexture` 右键 **Reimport** 换源图（不要改名或删掉重建）；
- 然后重新 Cook → `python gen_patch.py 1.0.0 1.1.0` → 启动客户端自动下载/校验/挂载 → **重启**看效果。

**重测同一版本补丁前建议清理客户端状态**（否则版本存档会被判「已最新」而不下载）：

```powershell
Remove-Item "<打包目录>\Saved\HotUpdate\*" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "<打包目录>\Saved\SaveGames\HotUpdateVersion.sav" -Force -ErrorAction SilentlyContinue
```

### 回滚与异常处理验证

右下角会显示 Slate 更新面板（自动开鼠标）。以下场景均应走 `Failed → RollingBack → Done`，不崩溃、日志区分清楚：

- **Mount 失败回滚**：把 `PatchServer/patches/*.pak` 换成垃圾字节，并把 `version.json` 的 `size` / `sha256` 改成该垃圾文件的实际值（→ 校验通过、Mount 失败）→ 版本重置为基础版、坏包删除、面板出现「重试」。
- **磁盘空间不足**：把 `version.json` 的 `size` 改成一个大于磁盘可用空间的巨大值（如 `2000000000000` = 2TB）→ 下载前预检**直接失败**（不重试）。
- **断网 / 清单错误**：停掉 HTTP 服务或改坏 `version.json` → 失败后 no-op 回滚，稳定版不变。
- **ForceUpdate 防死循环**：`min_supported_client` 设为 `1.1.0` 且补丁必失败 → 第二次启动日志「强制更新被跳过…请手动重试」。
- **重试**：`HotUpdate.RetryUpdate` 或面板「重试」按钮 → 清除失败记录并重新检测。

统计日志：`[HotUpdate][Stats] 补丁体积 / 下载耗时 / 校验耗时 / 挂载耗时 / 总耗时`。

---

## 八、热更新流程

```
打包 v1.0 基础包
   ↓
export_release.py 导出基线（Release_1.0.0.json）
   ↓
修改资源 + 重新 Cook
   ↓
gen_patch.py 生成增量 Pak（Patch_1.0.0_to_1.1.0.pak）+ version.json
   ↓
（服务端）python -m http.server 8000 提供 version.json / Pak
   ↓
客户端启动 → RemountStablePatch() 先挂载记录的稳定补丁
   ↓
拉清单比对 → UpToDate / NeedUpdate / ForceUpdate
   ↓  (ForceUpdate 自动下载，带防死循环守卫 / NeedUpdate 待确认)
下载 .tmp → SHA-256 + 大小校验 → 改名 Patch_<版本>_<sha8>.pak
   ↓
Mounting 挂载 → NotifyHotfixApplied 资源生效 → 写稳定版本 → Done
   ↓  失败（坏包 / 磁盘不足 / 断网 / 清单错误）
Failed → RollingBack（回滚：卸载坏包 + 删包 + 重置稳定版）→ Done
   ↓
重启 → 自动重挂载稳定补丁，资源与版本一致（UI/数值同步刷新）
```

---

## 九、已知局限与可扩展方向

- 不含依赖分析（只打入变更文件本身，对标 HotPatcher「包含依赖」待扩展）；
- 回滚目标固定为**基础版**（本 Demo 单补丁）；多跳升级需保留上一稳定补丁的 order 策略，属扩展；
- 挂载**不会替换已加载进内存的对象**，材质/贴图类需重启（启动重挂载）才可见；
- **改被 Cook 内容引用的 C++ 类结构后需重新 Cook/打包基础包**（新旧二进制序列化不匹配），只替换可执行文件不行；
- 下载未做断点续传/并发（决策边界）；
- 删除资源只列出、未实现删除补丁（资源热更通常不删资源，可忽略）。

---

## 十、相关文档

- 补丁生成 SOP（BuildScripts 使用说明）：`BuildScripts/README.md`
