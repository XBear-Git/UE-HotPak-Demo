# -*- coding: utf-8 -*-
"""
导出基础包基线清单（对标 HotPatcher ByRelease）

用法：
    python export_release.py <版本号>

示例：
    python export_release.py 1.0.0

作用：
    遍历 Cook 产物里 TRACKED_CONTENT_DIR 下的所有资源文件，
    对每个文件计算 SHA256 + 大小，写入 PatchServer/manifests/Release_<版本>.json。
    这份清单就是后续 gen_patch.py 做差异比对的「基线」。

注意：
    必须在「打好基础包、Cook 产物是最新 v1.0」时执行一次，
    并把生成的 JSON 存档，作为不可变的基线。
"""

import os
import sys
import json
import hashlib

import hotpatch_config as cfg


def sha256_of_file(path):
    """计算文件 SHA256（分块读，避免大文件撑爆内存）。"""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def scan_cooked_files():
    """
    遍历 Cook 产物中 TRACKED_CONTENT_DIR 下的资源文件。

    返回 {相对Content路径: {"sha256": ..., "size": ...}}
    """
    result = {}
    tracked_root = os.path.join(cfg.COOKED_CONTENT_ROOT, cfg.TRACKED_CONTENT_DIR)

    if not os.path.isdir(tracked_root):
        print("[export_release] 警告：找不到目录 {}".format(tracked_root))
        print("[export_release] 请先打包基础包，确保 Cook 产物存在。")
        return result

    for dirpath, _dirnames, filenames in os.walk(tracked_root):
        for fn in filenames:
            ext = os.path.splitext(fn)[1].lower()
            if ext not in cfg.ASSET_EXTENSIONS:
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, cfg.COOKED_CONTENT_ROOT).replace("\\", "/")
            result[rel] = {
                "sha256": sha256_of_file(full),
                "size": os.path.getsize(full),
            }

    return result


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "1.0.0"

    files = scan_cooked_files()
    if not files:
        print("[export_release] 没有扫描到任何资源，已中止。")
        sys.exit(1)

    os.makedirs(cfg.MANIFEST_DIR, exist_ok=True)

    manifest = {
        "version": version,
        "project": cfg.PROJECT_NAME,
        "engine": "5.7.4",
        "tracked_dir": cfg.TRACKED_CONTENT_DIR,
        "files": files,
    }

    out_path = os.path.join(cfg.MANIFEST_DIR, "Release_{}.json".format(version))
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print("[export_release] 基线 {} 已导出：{} 个文件 -> {}".format(version, len(files), out_path))


if __name__ == "__main__":
    main()
