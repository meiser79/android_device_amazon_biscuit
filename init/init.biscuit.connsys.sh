#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Fire OS runs the connsys WMT, PSM and BTIF RX threads as SCHED_FIFO
# (MAX_RT_PRIO - 2) on Biscuit so Bluetooth audio does not stutter under load.

for i in 1 2 3 4 5 6 7 8 9 10; do
    missing=0
    for thread in mtk_wmtd mtk_stp_psm btif_rxd; do
        pids=$(/system/bin/pidof "$thread")
        if [ -z "$pids" ]; then
            missing=1
            continue
        fi
        for pid in $pids; do
            /system/bin/chrt -f -p 98 "$pid"
        done
    done
    [ "$missing" = 0 ] && exit 0
    sleep 1
done
exit 1
