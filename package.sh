#!/usr/bin/env bash
set -euo pipefail

APP_NAME="MinesweeperAssistant"
OUT_DIR="dist/${APP_NAME}"
BIN_PATH="build/bin/${APP_NAME}.exe"

rm -rf dist
mkdir -p "${OUT_DIR}"

cp "${BIN_PATH}" "${OUT_DIR}/"
# 复制常见依赖（路径按 MSYS2 默认安装目录）
if [ -d /mingw64/bin ]; then
  cp /mingw64/bin/libopencv_*.dll "${OUT_DIR}/" || true
  cp /mingw64/bin/libgcc_s_seh-1.dll "${OUT_DIR}/" || true
  cp /mingw64/bin/libstdc++-6.dll "${OUT_DIR}/" || true
  cp /mingw64/bin/libwinpthread-1.dll "${OUT_DIR}/" || true
fi

echo "@echo off" > "${OUT_DIR}/start.bat"
echo "%~dp0${APP_NAME}.exe" >> "${OUT_DIR}/start.bat"

pushd dist >/dev/null
zip -r "${APP_NAME}.zip" "${APP_NAME}/"
popd >/dev/null
