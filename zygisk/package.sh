#!/usr/bin/env bash
# Package the built .so into a flashable Magisk/KernelSU zip.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"
SO="$OUT/zygisk/arm64-v8a.so"
STAGE="$OUT/zip"
ZIP="$OUT/DolbyRouter-zygisk.zip"

[ -f "$SO" ] || { echo "missing $SO - run build.sh first"; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/zygisk"
cp "$SO" "$STAGE/zygisk/arm64-v8a.so"
cp "$HERE/module.prop" "$STAGE/module.prop"
cp "$HERE/customize.sh" "$STAGE/customize.sh"

# optional manual override config (debug / without the app UI)
# The DolbyRouter app writes per-app scope to
#   /data/data/com.dolbyrouter/shared_prefs/config.xml
# and the module reads that automatically. This file is only used to force a
# global on/off or to allow-list a package without the app.
cat > "$STAGE/perapp.conf" <<'EOF'
# DolbyRouter manual config (optional)
# enabled=1     master switch (default on)
# one "app=<package>" per line; listed apps are forced onto the normal track
enabled=1
EOF

( cd "$STAGE" && rm -f "$ZIP" && zip -r -9 "$ZIP" . >/dev/null )
echo "OK -> $ZIP"
unzip -l "$ZIP"
