#!/usr/bin/env bash
# Build the Zygisk module .so.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NDK="${NDK:-${ANDROID_NDK_HOME:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}/ndk/27.2.12479018}}"
OUT="$HERE/out"
STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

cmake -S "$HERE/jni" -B "$OUT/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT/build"

mkdir -p "$OUT/zygisk"
cp "$OUT/build/arm64-v8a.so" "$OUT/zygisk/arm64-v8a.so"
"$STRIP" --strip-unneeded "$OUT/zygisk/arm64-v8a.so" 2>/dev/null || true
ls -la "$OUT/zygisk/arm64-v8a.so"
echo "OK -> $OUT/zygisk/arm64-v8a.so"
