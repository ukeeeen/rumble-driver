#!/bin/bash
# 编译 rumble_driver (aarch64) 并打包三个模块 zip：gamesir / stadia / combined
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"

# NDK 路径：优先环境变量，其次 Android SDK 默认位置
NDK_DIR="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK_DIR" ]; then
  SDK_DIR="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/AppData/Local/Android/Sdk}}"
  NDK_DIR="$(ls -d "$SDK_DIR"/ndk/android-ndk-* 2>/dev/null | head -1)"
fi
if [ -z "$NDK_DIR" ]; then echo "NDK 未找到：请设置 ANDROID_NDK_HOME 或 ANDROID_SDK_ROOT"; exit 1; fi
HOST_TAG="windows-x86_64"
case "$(uname -s)" in
  Darwin) HOST_TAG="darwin-x86_64" ;;   # Apple Silicon 也用该预编译包
  Linux)  HOST_TAG="linux-x86_64" ;;
esac
CLANG="$NDK_DIR/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang"
SYSROOT="$NDK_DIR/toolchains/llvm/prebuilt/$HOST_TAG/sysroot"
[ -x "$CLANG" ] || { echo "找不到 clang: $CLANG"; exit 1; }
echo "NDK: $NDK_DIR"

"$CLANG" --target=aarch64-linux-android24 --sysroot="$SYSROOT" -O2 \
  -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
  -o "$HERE/rumble_driver" "$HERE/gamesir_rumble.c"
ls -la "$HERE/rumble_driver"

# 打包：合集包 + GameSir 单独包 + Stadia 单独包
WROOT=$(cygpath -w "$ROOT")
cp "$HERE/rumble_driver" "$ROOT/modules/combined/rumble_driver"
cp "$HERE/rumble_driver" "$ROOT/modules/gamesir/rumble_driver"
cp "$HERE/rumble_driver" "$ROOT/modules/stadia/rumble_driver"
powershell -Command "Compress-Archive -Force -Path '$WROOT\\modules\\combined\\*' -DestinationPath '$WROOT\\rumble_drivers.zip'"
powershell -Command "Compress-Archive -Force -Path '$WROOT\\modules\\gamesir\\*' -DestinationPath '$WROOT\\gamesir_rumble.zip'"
powershell -Command "Compress-Archive -Force -Path '$WROOT\\modules\\stadia\\*' -DestinationPath '$WROOT\\stadia_rumble.zip'"
ls -la "$ROOT/rumble_drivers.zip" "$ROOT/gamesir_rumble.zip" "$ROOT/stadia_rumble.zip"
echo "打包完成：rumble_drivers.zip（合集）/ gamesir_rumble.zip（仅启明星）/ stadia_rumble.zip（仅Stadia）"
