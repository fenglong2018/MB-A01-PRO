#!/bin/sh
# 实测真实主频。
#
# rt_tick 由 SysTick 驱动，而 SysTick 按 SystemCoreClock 配置。拿 rt_tick 的
# 增量和墙上时间比，就能反推 SYSCLK 的真实值——这个时基在 RAM 里，调试器连接
# 不会干扰它（DWT_CYCCNT 会被 J-Link 每次连接重置，不能用）。
#
# 若实测频率明显低于 SystemCoreClock，说明外部晶振不是 HSE_VALUE 假设的频率，
# USB 那 48M 也就是假的。
set -e
cd "$(dirname "$0")/../.."

SECS=${1:-10}
TPS=${2:-1000}

# rt_tick 是 clock.c 里的 static，map 的全局符号表没有，得从 nm 取
addr=0x$(arm-none-eabi-nm build/User.elf | awk '$3=="rt_tick" {print $1; exit}')
if [ "$addr" = "0x" ]; then
    echo "找不到 rt_tick 符号"
    exit 1
fi
echo "rt_tick @ $addr, 采样 ${SECS}s"

printf 'halt\nmem32 %s 1\ngo\nqc\n' "$addr" > build/tick.jlink

read_tick() {
    JLinkExe -NoGui 1 -device N32WB452CE -if SWD -speed 1000 \
        -autoconnect 1 -CommandFile build/tick.jlink 2>&1 \
        | sed -n 's/^[0-9A-F]* = \([0-9A-F]*\).*/\1/p' | head -1
}

t0=$(read_tick); w0=$(date +%s.%N)
sleep "$SECS"
t1=$(read_tick); w1=$(date +%s.%N)

echo "tick: 0x$t0 -> 0x$t1"
awk -v t0="$t0" -v t1="$t1" -v w0="$w0" -v w1="$w1" -v tps="$TPS" '
BEGIN {
    d  = strtonum("0x" t1) - strtonum("0x" t0);
    dt = w1 - w0;
    printf "ticks=%d  wall=%.3fs  实测 tick 率=%.1f Hz (配置 %d Hz)\n", d, dt, d/dt, tps;
    printf "推算真实 SYSCLK = 配置值 x %.4f\n", (d/dt)/tps;
}'
