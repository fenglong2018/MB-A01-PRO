#!/bin/sh
# 查官方 DEMO 的时钟来源：晶振频率、SYSCLK 档位、USB 分频，以及工程里的编译宏。
LIB=/mnt/e/2026星海/省海渔北斗示位标/SOFT/shySOFT/v0.2/Nations.N32WB452_Library.2.6.0/Nations.N32WB452_Library.2.6.0
SYS=$LIB/firmware/CMSIS/device/system_n32wb452.c
VCP=$LIB/projects/n32wb452_EVAL/examples/USB/Virtual_COM_Port

echo "===== HSE_VALUE 定义 ====="
grep -n "define HSE_VALUE" "$LIB/firmware/CMSIS/device/n32wb452.h"

echo
echo "===== system_n32wb452.c 里的 SYSCLK 档位选择 ====="
grep -n "SYSCLK_FREQ" "$SYS" | head -30

echo
echo "===== USB 分频设置 ====="
grep -n "USBPRES\|USBCLK\|USB_PRE" "$SYS" | head -20

echo
echo "===== PLL 倍频设置 ====="
grep -n "PLLMULFCT\|PLL_MUL\|PLLSRC\|PLLHSEPRES" "$SYS" | head -30

echo
echo "===== VCP 工程 (MDK) 的预定义宏 ====="
tr ',' '\n' < "$VCP/MDK-ARM/virtual_com_port.uvprojx" | grep -i "define\|HSE\|SYSCLK" | head -20
