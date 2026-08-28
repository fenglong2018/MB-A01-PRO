#!/bin/sh
# 烧官方 VCP 例程的预编译 hex 做对照：它和本板一样假设 32MHz 晶振。
# 它能枚举就说明硬件没问题、锅在我们固件；它也不行就是板子的 USB 物理层。
# 恢复自己的固件用 make flash。
set -e
REPO=/mnt/e/2026星海/省海渔北斗示位标/SOFT/shySOFT/v0.2/gcc-rt-cursor
LIB=/mnt/e/2026星海/省海渔北斗示位标/SOFT/shySOFT/v0.2/Nations.N32WB452_Library.2.6.0/Nations.N32WB452_Library.2.6.0
HEX="$LIB/projects/n32wb452_EVAL/examples/USB/Virtual_COM_Port/MDK-ARM/Objects/Virtual_COM_Port.hex"

cd "$REPO"
cp "$HEX" build/demo_vcp.hex
printf 'r\nhalt\nerase\nloadfile build/demo_vcp.hex\nhalt\nr\ng\nqc\n' > build/demo_flash.jlink
JLinkExe -NoGui 1 -device N32WB452CE -if SWD -speed 1000 -autoconnect 1 \
    -CommandFile build/demo_flash.jlink 2>&1 | tail -12
