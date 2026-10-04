# -*- coding: utf-8 -*-
"""
HotPatchDemo 自研补丁管线 - 共享配置

所有脚本共用的路径常量。改路径只需要改这里。
"""

import os

# ---------------------------------------------------------------------------
# 引擎路径（UnrealPak.exe 所在引擎，按需修改）
# ---------------------------------------------------------------------------
UE_ROOT = r"D:\UE\UE_5.7"
UNREALPAK = os.path.join(UE_ROOT, "Engine", "Binaries", "Win64", "UnrealPak.exe")

# ---------------------------------------------------------------------------
# 项目路径（相对脚本位置自动推导，无需手改）
# BuildScripts 目录的上一级就是项目根目录
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
PROJECT_NAME = "HotPakDemo"

# Cook 产物根目录（.uasset/.uexp/.ubulk 等都在这里）
COOKED_CONTENT_ROOT = os.path.join(
    PROJECT_ROOT, "Saved", "Cooked", "Windows", PROJECT_NAME, "Content"
)

# 只跟踪「热更资源」目录（相对 Content 的路径）
# 引擎自身资源不参与 diff，因为它们是固定不变的
TRACKED_CONTENT_DIR = "HotPatchDemo"

# Pak 内挂载点前缀（与 UE 打包一致，运行时 Mount 对齐的关键）
MOUNT_POINT_PREFIX = "../../../{}/Content/".format(PROJECT_NAME)

# ---------------------------------------------------------------------------
# PatchServer 输出目录（模拟服务端）
#   manifests/  基线清单 Release_*.json
#   patches/    增量 Pak 及响应文件
#   full/       完整包（预留）
# ---------------------------------------------------------------------------
PATCH_SERVER = os.path.join(PROJECT_ROOT, "PatchServer")
MANIFEST_DIR = os.path.join(PATCH_SERVER, "manifests")
PATCH_DIR = os.path.join(PATCH_SERVER, "patches")

# 需要进 Pak 的资源扩展名
ASSET_EXTENSIONS = {".uasset", ".uexp", ".ubulk", ".umap"}

# 增量 Pak 挂载优先级（高于基础包 order=0）
PATCH_PAK_ORDER = 100
