# 自研补丁管线使用说明（SOP）

对标 HotPatcher 的 ByRelease / ByPatch，用 UnrealPak + SHA256 文件级 diff 实现的最小补丁生成管线。

## 脚本一览

| 脚本 | 作用 | 对标 |
|---|---|---|
| `hotpatch_config.py` | 共享路径配置 | — |
| `export_release.py` | 导出基础包基线清单 | ByRelease |
| `gen_patch.py` | diff + 打增量 Pak + version.json | ByPatch |

## 完整流程

### 1. 打包基础包 v1.0（一次性）

- 编辑器 `File > Package Project > Windows`，产出基础包（含 `pakchunk0-Windows.pak`）。
- **把 v1.0 基础包目录冻结存档**（例如 `D:\UE\UE_ExportExe\Windows\HotPakDemo\`），它是运行时跑的那份，不能动。

### 2. 导出基线清单（一次性）

```powershell
cd BuildScripts
python export_release.py 1.0.0
```

生成 `PatchServer/manifests/Release_1.0.0.json`，记录每个资源的 SHA256 + 大小。**存档，作为不可变基线。**

### 3. 修改资源

在编辑器里改资源（改 DataTable 数值 / 换贴图 / 改关卡蓝图），保存。

### 4. 重新 Cook（关键步骤）

改了源资源后，`Saved/Cooked` 里的 Cook 产物**不会自动更新**，必须重新 Cook 才能被 diff 出来。两种方式：

- **方式 A（推荐，快）**：编辑器菜单 `File > Cook Content for Windows`，只 Cook 不打包。
- **方式 B（慢但稳）**：重新 `Package Project`（重新 Cook + 打包，新 pakchunk0 忽略即可，我们只用到它的 Cook 产物）。

### 5. 生成增量 Pak

```powershell
cd BuildScripts
python gen_patch.py 1.0.0 1.1.0
```

- 输出 `PatchServer/patches/Patch_1.0.0_to_1.1.0.pak`（增量包）
- 输出 `PatchServer/version.json`（服务端版本清单，含 URL/大小/SHA256）

### 6. 验证生效

- 把增量 Pak 拷到基础包 `Content\Paks\` 目录 → 重启游戏 → 看到新资源（自动挂载）。
- 或运行时控制台执行 `HotUpdate.MountPak <pak路径>` 手动挂载验证。

## 原理一句话

**Cook 产物文件级 SHA256 diff**：基线打包后记录资源哈希 → 改资源重新 Cook → 对比哈希找出变更文件 → UnrealPak 把变更文件打成增量 Pak（挂载点 `../../../HotPakDemo/Content/`，运行时 order=100 覆盖基础包 order=0）。

## 已知局限（后续可扩展）

1. **不含依赖分析**：目前只打入「变更文件本身」。若新增资源被其它未变资源软引用，需手动确认依赖一起进包（对标 HotPatcher 的「包含依赖」）。
2. **不含删除资源处理**：目前只检测并列出删除，未实现删除补丁（资源热更通常不删资源，可忽略）。
3. **单平台**：只面向 Windows。

## 环境锁定

- 引擎：UE 5.7.4（Changelist 51494982）
- Python：3.12+
- UnrealPak：`D:\UE\UE_5.7\Engine\Binaries\Win64\UnrealPak.exe`
