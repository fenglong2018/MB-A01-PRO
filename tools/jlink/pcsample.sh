#!/bin/sh
# 采样 CPU 平时停在哪：连续 halt 几次取 PC，再用 addr2line 解析成函数/行号。
# CPU 若大部分时间在 WFI，这里会稳定落在睡眠相关的代码上。
set -e
cd "$(dirname "$0")/../.."

N=${1:-6}
i=0
while [ "$i" -lt "$N" ]; do
    pc=$(JLinkExe -NoGui 1 -device N32WB452CE -if SWD -speed 1000 \
         -autoconnect 1 -CommandFile tools/jlink/pc.jlink 2>&1 \
         | sed -n 's/^PC = \([0-9A-Fa-f]*\).*/\1/p' | head -1)
    if [ -n "$pc" ]; then
        printf '0x%s  %s\n' "$pc" \
            "$(arm-none-eabi-addr2line -f -e build/User.elf "0x$pc" | tr '\n' ' ')"
    else
        echo "(no PC)"
    fi
    i=$((i + 1))
    sleep 1
done
