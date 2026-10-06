# -*- coding: utf-8 -*-
"""
校验补丁 Pak 内容完整性（对标需求 FR-11）

用法：
    python verify_patch.py [基线版本] [新版本]
    # 默认： python verify_patch.py 1.0.0 1.1.0

作用：
    用 UnrealPak -List 列出补丁 Pak 内的资源，做三重比对，报告「缺失 / 多余」：
      1) 期望集：当前 Cook 产物相对基线清单 Release_<基线>.json 的「变更文件」；
      2) 响应文件：Patch_<基线>_to_<新>.txt 里声明要打进 Pak 的文件；
      3) Pak 实际内容：UnrealPak -List 列出的文件。
    三者应一致；不一致会明确指出差在哪一步（diff 逻辑 / UnrealPak / 打包集合）。

退出码：0 = 全部一致；1 = 存在差异或出错。
"""

import os
import re
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
    """扫描当前 Cook 产物，返回 {相对 Content 路径: {sha256,size}}。"""
    result = {}
    tracked_root = os.path.join(cfg.COOKED_CONTENT_ROOT, cfg.TRACKED_CONTENT_DIR)
    if not os.path.isdir(tracked_root):
        print("[verify] 警告：找不到 Cook 目录 {}".format(tracked_root))
        return result
    for dirpath, _dirnames, filenames in os.walk(tracked_root):
        for fn in filenames:
            if os.path.splitext(fn)[1].lower() not in cfg.ASSET_EXTENSIONS:
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, cfg.COOKED_CONTENT_ROOT).replace("\\", "/")
            result[rel] = {"sha256": sha256_of_file(full), "size": os.path.getsize(full)}
    return result


def diff_against_base(base_files, current_files):
    changed = {}
    for rel, info in current_files.items():
        if rel not in base_files or base_files[rel]["sha256"] != info["sha256"]:
            changed[rel] = info
    removed = [rel for rel in base_files if rel not in current_files]
    return changed, removed


def read_response_file(resp_path):
    """读响应文件，返回 dest 虚拟路径列表（第二个引号字段）。"""
    dests = []
    with open(resp_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            fields = re.findall(r'"([^"]*)"', line)
            if len(fields) >= 2:
                dests.append(fields[1].replace("\\", "/"))
    return dests


def list_pak(pak_path):
    """调 UnrealPak -List，返回 {pak 内路径: size}。"""
    cmd = [cfg.UNREALPAK, pak_path, "-List"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    out = (result.stdout or "") + (result.stderr or "")
    entries = {}
    for line in out.splitlines():
        m = re.search(r'"([^"]+)"\s+offset:\s*\d+,\s*size:\s*(\d+)', line)
        if m:
            entries[m.group(1).replace("\\", "/")] = int(m.group(2))
    return entries


def common_dir_prefix(paths):
    """求一组路径的最长公共目录前缀（以 '/' 结尾）。"""
    if not paths:
        return ""
    norm = [p.replace("\\", "/") for p in paths]
    # 逐字符求最长公共前缀
    prefix = norm[0]
    for p in norm[1:]:
        n = min(len(prefix), len(p))
        i = 0
        while i < n and prefix[i] == p[i]:
            i += 1
        prefix = prefix[:i]
        if not prefix:
            return ""
    # 截断到最后一个 '/'
    idx = prefix.rfind("/")
    return prefix[:idx + 1] if idx >= 0 else ""


def report_diff(title, missing, extra):
    ok = (not missing and not extra)
    print("  [{}] {}".format("OK" if ok else "差异", title))
    for m in sorted(missing):
        print("      - 缺失: {}".format(m))
    for e in sorted(extra):
        print("      + 多余: {}".format(e))
    return ok


def main():
    base_ver = sys.argv[1] if len(sys.argv) > 1 else "1.0.0"
    new_ver = sys.argv[2] if len(sys.argv) > 2 else "1.1.0"

    base_path = os.path.join(cfg.MANIFEST_DIR, "Release_{}.json".format(base_ver))
    pak_path = os.path.join(cfg.PATCH_DIR, "Patch_{}_to_{}.pak".format(base_ver, new_ver))
    resp_path = os.path.join(cfg.PATCH_DIR, "Patch_{}_to_{}.txt".format(base_ver, new_ver))

    print("[verify] 基线清单: {}".format(base_path))
    print("[verify] 补丁 Pak : {}".format(pak_path))
    if not os.path.exists(base_path):
        print("[verify] 找不到基线清单，请先 export_release.py {}".format(base_ver))
        sys.exit(1)
    if not os.path.exists(pak_path):
        print("[verify] 找不到补丁 Pak，请先 gen_patch.py {} {}".format(base_ver, new_ver))
        sys.exit(1)

    # 1) 期望集：当前 Cook 相对基线的变更文件
    with open(base_path, "r", encoding="utf-8") as f:
        base_files = json.load(f).get("files", {})
    current_files = scan_cooked_files()
    changed, removed = diff_against_base(base_files, current_files)
    print("[verify] Cook diff：变更 {} 个，删除 {} 个".format(len(changed), len(removed)))

    # 2) 响应文件声明的集合（dest 去掉挂载点前缀 → rel）
    if os.path.exists(resp_path):
        dests = read_response_file(resp_path)
        packed_rel = set()
        for dest in dests:
            if dest.startswith(cfg.MOUNT_POINT_PREFIX):
                packed_rel.add(dest[len(cfg.MOUNT_POINT_PREFIX):])
            else:
                packed_rel.add(dest)
    else:
        print("[verify] 警告：找不到响应文件，改用期望集推断")
        dests = [cfg.MOUNT_POINT_PREFIX + rel for rel in changed]
        packed_rel = set(changed.keys())

    # 3) Pak 实际内容
    listed = list_pak(pak_path)
    mount_prefix = common_dir_prefix(dests)
    expected_internal = set()
    for dest in dests:
        expected_internal.add(dest[len(mount_prefix):] if dest.startswith(mount_prefix) else dest)

    print("[verify] Pak 内文件 {} 个，声明挂载点 {}".format(len(listed), mount_prefix or "<无>"))
    print("")

    all_ok = True

    # 检查一：diff 出来的变更集，是否都被响应文件声明
    print("[verify] 检查 1/3：Cook diff 变更集 vs 响应文件声明集")
    all_ok &= report_diff(
        "变更文件是否都进了响应文件",
        missing=set(changed.keys()) - packed_rel,
        extra=packed_rel - set(changed.keys()),
    )

    # 检查二：Pak 实际内容 vs 响应文件声明
    print("[verify] 检查 2/3：Pak 实际内容 vs 响应文件声明集")
    all_ok &= report_diff(
        "Pak 是否与响应文件一致",
        missing=expected_internal - set(listed.keys()),
        extra=set(listed.keys()) - expected_internal,
    )

    # 检查三：Pak 内文件大小 vs Cook 产物大小（同 rel 相比）
    print("[verify] 检查 3/3：Pak 内文件大小 vs Cook 产物大小")
    size_mismatch = []
    for rel, info in changed.items():
        internal = rel[len(cfg.TRACKED_CONTENT_DIR) + 1:] if rel.startswith(cfg.TRACKED_CONTENT_DIR + "/") else rel
        # Pak 内路径通常是相对挂载点（例如 DataTables/xxx），用 basename + 目录尾部匹配
        candidates = [k for k in listed if k.endswith(internal) or internal.endswith(k)]
        if not candidates:
            continue
        if all(listed[c] != info["size"] for c in candidates):
            size_mismatch.append("{}（Cook={}，Pak={}）".format(rel, info["size"], [listed[c] for c in candidates]))
    all_ok &= report_diff("Pak 内文件大小是否与 Cook 一致", missing=size_mismatch, extra=[])

    print("")
    if all_ok and not removed:
        print("[verify] 结果：通过 ✅  补丁 Pak 内容与 Cook 变更集一致。")
        sys.exit(0)
    else:
        if removed:
            print("[verify] 注意：存在被删除的资源（当前管线不含删除补丁）：")
            for r in removed:
                print("      x {}".format(r))
        print("[verify] 结果：存在差异 ❌  请检查上面的缺失/多余/大小项。")
        sys.exit(1)


if __name__ == "__main__":
    main()
