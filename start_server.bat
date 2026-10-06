@echo off
REM ============================================================
REM  一键启动本地 HTTP 服务（把 PatchServer\ 当静态站点，端口 8000）
REM  双击运行，或命令行执行本文件。
REM ============================================================
cd /d "%~dp0PatchServer"

if not exist "version.json" (
    echo [error] 找不到 PatchServer\version.json，请先在 BuildScripts 里运行 gen_patch.py。
    pause
    exit /b 1
)

echo ============================================================
echo  Serving: %CD%
echo  版本清单: http://127.0.0.1:8000/version.json
echo  按 Ctrl+C 停止。
echo ============================================================
python -m http.server 8000

pause
