#!/system/bin/sh
# Magisk / KernelSU Next installer script for the DolbyRouter Zygisk module.
SKIPUNZIP=1

ui_print "- DolbyRouter Zygisk module"

mkdir -p "$MODPATH/zygisk"
unzip -o "$ZIPFILE" 'zygisk/*' -d "$MODPATH" >&2
set_perm_recursive "$MODPATH" 0 0 0755 0644

ui_print "- installed: $(ls -1 "$MODPATH/zygisk" 2>/dev/null | tr '\n' ' ')"
