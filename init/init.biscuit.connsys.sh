#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Fire OS runs the connsys WMT, PSM and BTIF RX threads as SCHED_FIFO
# (MAX_RT_PRIO - 2) on Biscuit so Bluetooth audio does not stutter under load.
# The 3.18 kernel only has mtk_wmtd as a thread: BTIF RX is a workqueue that
# the driver already makes real-time, and PSM runs as work. Boost whichever
# of them exist once mtk_wmtd is up.

for i in 1 2 3 4 5 6 7 8 9 10; do
    [ -n "$(/system/bin/pidof mtk_wmtd)" ] && break
    sleep 1
done
[ -n "$(/system/bin/pidof mtk_wmtd)" ] || exit 1

for thread in mtk_wmtd mtk_stp_psm btif_rxd; do
    for pid in $(/system/bin/pidof "$thread"); do
        /system/bin/chrt -f -p 98 "$pid"
    done
done
exit 0
