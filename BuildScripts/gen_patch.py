# -*- coding: utf-8 -*-
"""
生成增量补丁 Pak（对标 HotPatcher ByPatch）

用法：
    python gen_patch.py <基线版本> <新版本>

示例：
    python gen_patch.py 1.0.0 1.1.0

流程：
    1. 读基线清单 PatchServer/manifests/Release_<基线版本>.json
    2. 扫描当前 Cook 产物，与基线做 SHA256 差异比对
    3. 找出「新增 / 修改」的文件，生成 UnrealPak 响应文件
    4. 调用 UnrealPak.exe 打成 Patch_<基线>_to_<新版本>.pak
    5. 生成服务端版本清单 PatchServer/version.json（含 URL/大小/SHA256）

前提：
    改完资源后要先重新 Cook / 重新打包，让 Saved/Cooked 更新，
    本脚本才能比对出差异。
"""

import os
import sys
import json
import hashlib
import subprocess

import hotpatch_config as cfg


def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def scan_cooked_files():
    result = {}
    tracked_root = os.path.join(cfg.COOKED_CONTENT_ROOT, cfg.TRACKED_CONTENT_DIR)
    if not os.path.isdir(tracked_root):
        print("[gen_patch] 警告：找不到目录 {}".format(tracked_root))
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


def diff_against_base(base_files, current_files):
    """
    对比基线，返回 (变更文件, 删除文件)。
    变更文件 = 新增 + 哈希变化的修改文件。
    """
    changed = {}
    removed = []

    for rel, info in current_files.items():
        if rel not in base_files:
            changed[rel] = info  # 新增
        elif base_files[rel]["sha256"] != info["sha256"]:
            changed[rel] = info  # 修改

    for rel in base_files:
        if rel not in current_files:
            removed.append(rel)  # 删除

    return changed, removed


def build_response_file(changed_files, resp_path):
    """
    生成 UnrealPak 响应文件。

    每行格式： "磁盘绝对路径" "pak内虚拟路径"
    pak 内虚拟路径带挂载点前缀，与 UE 打包保持一致。
    """
    lines = []
    for rel in sorted(changed_files):
        source = os.path.join(cfg.COOKED_CONTENT_ROOT, rel.replace("/", os.sep))
        dest = cfg.MOUNT_POINT_PREFIX + rel
        lines.append('"{}" "{}"'.format(source, dest))

    with open(resp_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    return len(lines)


def run_unrealpak(resp_path, out_pak):
    """调用 UnrealPak.exe 打包。"""
    cmd = [cfg.UNREALPAK, out_pak, "-Create={}".format(resp_path)]
    print("[gen_patch] 执行: {}".format(" ".join(cmd)))
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.stdout:
        # 只打印关键几行，避免刷屏
        for line in result.stdout.splitlines()[-5:]:
            print("  " + line)

    if result.returncode != 0 or not os.path.exists(out_pak):
        if result.stderr:
            print(result.stderr)
        raise RuntimeError("UnrealPak 打包失败，返回码 {}".format(result.returncode))


def main():
    base_ver = sys.argv[1] if len(sys.argv) > 1 else "1.0.0"
    new_ver = sys.argv[2] if len(sys.argv) > 2 else "1.1.0"

    # 1. 读基线
    base_path = os.path.join(cfg.MANIFEST_DIR, "Release_{}.json".format(base_ver))
    if not os.path.exists(base_path):
        print("[gen_patch] 找不到基线清单：{}".format(base_path))
        print("[gen_patch] 请先运行 export_release.py {} 导出基线。".format(base_ver))
        sys.exit(1)
    with open(base_path, "r", encoding="utf-8") as f:
        base = json.load(f)
    base_files = base.get("files", {})

    # 2. 扫描当前 + diff
    current_files = scan_cooked_files()
    changed, removed = diff_against_base(base_files, current_files)

    if not changed and not removed:
        print("[gen_patch] 没有检测到资源变更，无需生成补丁。")
        return

    print("[gen_patch] 变更 {} 个，删除 {} 个：".format(len(changed), len(removed)))
    for rel in sorted(changed):
        print("  [改/增] " + rel)
    for rel in removed:
        print("  [删除] " + rel)

    # 3. 生成响应文件 + 打 Pak
    os.makedirs(cfg.PATCH_DIR, exist_ok=True)
    pak_name = "Patch_{}_to_{}.pak".format(base_ver, new_ver)
    out_pak = os.path.join(cfg.PATCH_DIR, pak_name)
    resp_path = os.path.join(cfg.PATCH_DIR, "Patch_{}_to_{}.txt".format(base_ver, new_ver))

    build_response_file(changed, resp_path)
    run_unrealpak(resp_path, out_pak)

    # 4. 生成服务端版本清单 version.json
    pak_sha = sha256_of_file(out_pak)
    pak_size = os.path.getsize(out_pak)
    version_json = {
        "latest_version": new_ver,
        "min_supported_client": base_ver,
        "force_update": False,
        "pak": {
            "url": "http://127.0.0.1:8000/patches/{}".format(pak_name),
            "size": pak_size,
            "sha256": pak_sha,
            "order": cfg.PATCH_PAK_ORDER,
            "mount_point": cfg.MOUNT_POINT_PREFIX,
        },
        "changelog": "自研补丁管线生成的增量包 {} -> {}".format(base_ver, new_ver),
    }
    out_vj = os.path.join(cfg.PATCH_SERVER, "version.json")
    with open(out_vj, "w", encoding="utf-8") as f:
        json.dump(version_json, f, indent=2, ensure_ascii=False)

    print("[gen_patch] 补丁已生成：{} ({} 字节)".format(out_pak, pak_size))
    print("[gen_patch] 版本清单已生成：{}".format(out_vj))


if __name__ == "__main__":
    main()
