#!/usr/bin/env bash
# build-and-run.sh —— spike 一键构建并运行（Git Bash 环境）。
# 用法: ./build-and-run.sh
set -euo pipefail
cd "$(dirname "$0")"

echo "== [1/3] dotnet build managed（net8.0 类库） =="
dotnet build managed/Hello.csproj -c Release

echo "== [2/3] cl 编译 host.exe =="
(cd host && cmd //c build.cmd)

echo "== [3/3] 运行 host.exe =="
./host/host.exe
