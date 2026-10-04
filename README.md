# HotPakDemo

基于 **Unreal Engine 5.7** 的客户端资源热更新（Pak 补丁）Demo 项目。

核心目标：在不重新安装整包的前提下，通过运行时挂载增量 Pak，实现「改资源 → 生成补丁 → 客户端挂载 → 生效」的完整闭环。

---

## 一、项目简介

本项目实现了一套**传统 Pak 路线的资源热更新框架**：

- **服务端**维护版本清单（`version.json`）与增量 Pak；
- **客户端**启动时拉取清单做差异比对，仅下载变更的增量 Pak，校验后运行时挂载生效；
- 挂载失败支持回滚（规划中）。

当前阶段（Day 1 ~ Day 3）已跑通**手动热更新闭环**与**客户端启动自动版本检测**，后续逐步接入「下载 → 校验 → 挂载」的自动化流程。

---

## 二、已实现功能

| 功能 | 说明 |
|---|---|
| ✅ 关闭 IoStore，走传统 Pak | `DefaultGame.ini` 中 `bUseIoStore=False` / `bUseZenStore=False` / `bUsePakFile=True`，是热更新的前提 |
| ✅ 运行时手动 Pak 挂载 | `UHotUpdateSubsystem::MountPak`，封装 `FPakPlatformFile::Mount`，支持控制台命令触发 |
| ✅ 热更新验证框架 | 子系统（状态/回调）+ 测试 Actor（四类资源槽）+ DataTable 行结构，三层解耦 |
| ✅ 自研补丁生成管线 | Python 脚本：导出基线 → SHA256 diff → UnrealPak 打增量 Pak |
| ✅ 服务端版本清单 | `PatchServer/version.json`（版本号、URL、大小、SHA256、挂载点） |
| ✅ 远端版本清单拉取与比对 | `UVersionManager` 用 `FHttpModule` 异步 GET `version.json`，按**数字分段**比较版本，输出「需更新 / 已最新 / 强制更新」 |
| ✅ 更新流程状态机 | `UUpdateStateMachine` 定义 `EHotUpdateState`（`Idle → Checking → UpToDate / NeedUpdate / ForceUpdate` 已实现，下载/校验/挂载等为占位），每次转移打印日志并广播 |
| ✅ 本地版本持久化 | `UVersionRecord`（`USaveGame`）写入固定槽 `HotUpdateVersion`，作为下次启动比对基线（FR-08） |
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
│   ├── Core/                        核心逻辑（挂载子系统、更新状态机、版本管理/持久化、测试 Actor）
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
| `Core/HotUpdateSubsystem.h/.cpp` | **核心/编排**。`UGameInstanceSubsystem`，`Initialize` 时创建状态机与版本管理器并自动发起一次检测；提供运行时 Pak 挂载（`MountPak`）、挂载状态、回调契约（`OnHotfixApplied`）、更新状态广播（`OnUpdateStateChanged`）。注册控制台命令 `HotUpdate.MountPak` / `HotUpdate.CheckForUpdate` / `HotUpdate.Status` |
| `Core/UpdateStateMachine.h/.cpp` | 更新状态机。`EHotUpdateState` 枚举（`Idle/Checking/UpToDate/NeedUpdate/ForceUpdate` 已实现，`Downloading/Verifying/Mounting/Done/Failed/RollingBack` 为 Day 4~6 占位）；`TransitionTo()` 打印日志并广播 `OnStateChanged` |
| `Core/VersionManager.h/.cpp` | 版本检测与比对。`FHttpModule` 异步 GET `version.json`，解析 `latest_version` / `min_supported_client` / `force_update`，`CompareVersion()` 按数字分段比较（避免 `"1.10" < "1.9"`），判定三分支并驱动状态机 |
| `Core/VersionRecord.h/.cpp` | 本地版本持久化（`USaveGame`）。槽名固定 `HotUpdateVersion`，提供 `SaveVersion()` / `LoadVersion()`，作为下次启动比对基线（FR-08） |
| `Core/BaseHotTestActor.h/.cpp` | 热更验证场景的执行器。持有四类软引用资源槽（贴图/数据表/Actor/关卡），观察子系统回调，提供 `ForceReloadAllSlots()` 手动刷新入口 |
| `Structs/ST_HotfixConfig.h` | `FTableRowBase` 子类，DataTable 行结构（版本号、倍率、生命、标题、开关、颜色等），用于验证数据热更 |
| `HotPakDemo.cpp/.h` | 主模块 + `LogHotUpdate` 日志分类 |

### 关键实现说明

- **挂载点对齐**：`MountPak` 调用 `FPakPlatformFile::Mount(pakPath, order, nullptr)`，传 `nullptr` 让引擎使用 Pak 自带的挂载点，自动对齐（避免 MountPoint 踩坑）。
- **覆盖优先级**：增量 Pak 以 `PakOrder = 100` 挂载，高于基础包（order 0），资源查找时补丁优先。
- **可观测性**：挂载成功/失败用 `AddOnScreenDebugMessage` 在屏幕显示（绿/红），同时 `UE_LOG(LogHotUpdate, ...)` 记录到日志文件。
- **异步安全**：HTTP 回调通过 `BindUObject`（弱引用）绑定，对象销毁后不会悬空调用。

---

## 六、补丁生成脚本（`BuildScripts/`）

| 脚本 | 作用 |
|---|---|
| `hotpatch_config.py` | 共享路径配置（引擎路径、Cook 目录、挂载点前缀、PatchServer 输出目录） |
| `export_release.py` | 导出基础包基线清单 `Release_<版本>.json`（记录每个资源的 SHA256 + 大小） |
| `gen_patch.py` | 读取基线 → SHA256 diff 找变更文件 → 生成响应文件 → 调 UnrealPak 打增量 Pak → 生成 `version.json` |
| `README.md` | 补丁生成 SOP（详细步骤） |

**核心原理**：Cook 产物文件级 SHA256 diff——基线打包后记录资源哈希，改资源重新 Cook 后对比哈希找出变更文件，用 UnrealPak 打成增量 Pak（挂载点 `../../../HotPakDemo/Content/`）。

---

## 七、构建与运行

### 编译

```powershell
# Game 目标（运行打包版）
D:\UE\UE_5.7\Engine\Build\BatchFiles\Build.bat HotPakDemo Win64 Development <项目路径>\HotPakDemo.uproject -waitmutex
```

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

### 版本检测验证（Day 3）

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
| 已最新 | `1.0.0` | `1.0.0` | `Checking → UpToDate`（并把远端版本写入本地存档） |

本地版本存档位置：`<打包目录>\Saved\SaveGames\HotUpdateVersion.sav`，删除它即可重置为默认 `1.0.0`。

---

## 八、热更新流程（当前阶段）

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
客户端启动 → 拉清单比对 → UpToDate / NeedUpdate / ForceUpdate   ← Day 3 已实现
   ↓
下载 → 校验 → 挂载 → 资源生效                                   ← Day 4 / Day 5 待接入
```

---

## 九、已知局限与待办

**当前局限**（自研管线的最小实现）：
- 不含依赖分析（只打入变更文件本身，对标 HotPatcher「包含依赖」待扩展）；
- 版本检测已自动化，但**下载 / 校验 / 挂载**尚未接入；补丁挂载目前仍靠手动控制台命令触发；
- 本地版本在「已最新」分支写入（Day 3 验证用），「挂载成功后写入」留待 Day 5；
- 删除资源只列出、未实现删除补丁（资源热更通常不删资源，可忽略）。

**后续规划（Day 4 ~ Day 6）**：
- [x] Day 3：`UVersionManager`（拉清单/比对/持久化）+ 更新状态机 + 本地 HTTP 服务
- [ ] Day 4：`UPakDownloader`（FHttpModule 下载）+ `.tmp` 落地 + SHA256 校验
- [ ] Day 5：整合挂载流程（下载 → 校验 → 挂载 → 资源生效）
- [ ] Day 6：回滚 + 异常处理 + 更新流程 UI

---

## 十、相关文档

- 调研笔记（Day 1 概念 + 踩坑记录）：`D:\AgentContext\资源热更\调研笔记.md`
- Day 3 工作流程文档（客户端更新器骨架设计）：`D:\AgentContext\资源热更\Day3工作流程文档.md`
- 补丁生成 SOP：`BuildScripts/README.md`

---

> 本 README 随项目进度持续更新。
