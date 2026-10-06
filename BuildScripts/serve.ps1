# 一键启动本地 HTTP 静态服务（把 PatchServer/ 当站点，供客户端拉取 version.json 与补丁 Pak）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File BuildScripts\serve.ps1          # 默认端口 8000
#   powershell -ExecutionPolicy Bypass -File BuildScripts\serve.ps1 9000     # 指定端口

param([int]$Port = 8000)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$patchServer = Join-Path $projectRoot "PatchServer"

if (-not (Test-Path $patchServer)) {
    Write-Error "找不到 PatchServer 目录：$patchServer"
    exit 1
}
if (-not (Test-Path (Join-Path $patchServer "version.json"))) {
    Write-Error "找不到 $patchServer\version.json（请先跑 gen_patch.py）"
    exit 1
}

Write-Host "Serving : $patchServer"
Write-Host "清单地址: http://127.0.0.1:$Port/version.json"
Write-Host "按 Ctrl+C 停止。"
Write-Host ""

Push-Location $patchServer
try {
    python -m http.server $Port
}
finally {
    Pop-Location
}
