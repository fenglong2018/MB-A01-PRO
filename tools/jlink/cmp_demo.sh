#!/bin/sh
# 把本仓库的 USB 驱动 / VCP 移植层跟官方库原版逐文件比对，
# 找出与「能枚举的 DEMO」之间的真实差异。
LIB=/mnt/e/2026星海/省海渔北斗示位标/SOFT/shySOFT/v0.2/Nations.N32WB452_Library.2.6.0/Nations.N32WB452_Library.2.6.0
REPO=/mnt/e/2026星海/省海渔北斗示位标/SOFT/shySOFT/v0.2/gcc-rt-cursor
VCP=$LIB/projects/n32wb452_EVAL/examples/USB/Virtual_COM_Port

echo "===== usbfs_driver 目录差异 ====="
diff -rq "$LIB/firmware/n32wb452_usbfs_driver" "$REPO/firmware/n32wb452_usbfs_driver" 2>&1

echo
echo "===== VCP 例程文件清单 ====="
find "$VCP" -type f 2>&1 | sed "s|$VCP/||"

echo
echo "===== 逐文件对比 VCP 移植层 ====="
for f in usb_desc.c usb_desc.h usb_prop.c usb_prop.h usb_conf.h usb_endp.c usb_istr.c usb_pwr.c; do
    src=$(find "$VCP" -name "$f" 2>/dev/null | head -1)
    dst=$(find "$REPO/services/usb" -name "$f" 2>/dev/null | head -1)
    if [ -n "$src" ] && [ -n "$dst" ]; then
        n=$(diff "$src" "$dst" | grep -c '^[<>]')
        echo "--- $f : $n 行差异"
    else
        echo "--- $f : 缺失 (demo=$src repo=$dst)"
    fi
done
