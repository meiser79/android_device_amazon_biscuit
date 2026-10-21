#!/bin/sh
# Update a Biscuit (Echo Dot 2nd Gen) over Wi-Fi or USB: push the ROM zip
# to /sdcard over adb, queue it in TWRP's OpenRecoveryScript and reboot
# into recovery. TWRP installs it to the current slot and reboots. If the
# Dot is already in amonet's TWRP (USB only, TWRP has no Wi-Fi), the zip
# is installed right away with "twrp install" instead.
#
# Usage: biscuit-ota.sh [-f] [-s <a|b>] <ip|usb> <rom.zip>
#    or: IP=<ip|usb> ZIP=<rom.zip> [FORCE=1] [SLOT=<a|b>] biscuit-ota.sh
#
# -f (or FORCE=1) skips the checks that the device is a Biscuit and that
# TWRP is amonet v2's, for other Echo devices whose TWRP runs an
# OpenRecoveryScript too. The zip's own updater still decides whether it
# installs on that device.
#
# -s <a|b> (or SLOT=) installs into that slot and boots it. TWRP links
# current-boot and current-system to the slot it booted on, so it has to
# boot on that slot before it installs. Over USB the script sets the slot
# active with bcbtool in TWRP and reboots it. Over Wi-Fi (TWRP has none)
# it queues two OpenRecoveryScripts instead: the first sets the slot
# active and queues the second, then reboots into recovery; the second
# checks that TWRP now runs on that slot, installs and reboots. If the
# switch fails, TWRP stays up on the old slot and installs nothing; if the
# install fails, the new slot is active and TWRP stays up, so going back
# needs USB.
#
# Run it on the PC. Over Wi-Fi, the ROM's adbd listens on port 5555
# (mt8163-common's init sets service.adb.tcp.port). With "usb", exactly one
# device must be attached over USB, or SERIAL names it. The zip lands in
# /sdcard/update.zip.

set -eu

die() { echo "error: $*" >&2; exit 1; }

USAGE="usage: $0 [-f] [-s <a|b>] <ip|usb> <rom.zip>"
FORCE=${FORCE:-}
SLOT_WANTED=${SLOT:-}
while [ $# -gt 0 ]; do
    case "$1" in
        -f) FORCE=1; shift ;;
        -s) [ $# -ge 2 ] || die "$USAGE"; SLOT_WANTED=$2; shift 2 ;;
        -*) die "$USAGE" ;;
        *) break ;;
    esac
done
IP=${1:-${IP:-}}
ZIP=${2:-${ZIP:-}}
PORT=${PORT:-5555}

[ -n "$IP" ] && [ -n "$ZIP" ] || die "$USAGE"
case "$SLOT_WANTED" in
    ''|a|b) ;;
    *) die "slot must be a or b, not '$SLOT_WANTED'" ;;
esac

MISSING=
for tool in adb awk grep sed seq sha256sum timeout unzip; do
    command -v "$tool" >/dev/null 2>&1 || MISSING="$MISSING $tool"
done
[ -z "$MISSING" ] || die "missing tools:$MISSING (install them first)"

[ -f "$ZIP" ] || die "$ZIP not found"
unzip -tq "$ZIP" >/dev/null 2>&1 || die "$ZIP is not a valid zip"

REMOTE=/sdcard/update.zip
ORS=/cache/recovery/openrecoveryscript

if [ "$IP" = usb ]; then
    USB=1
    if [ -n "${SERIAL:-}" ]; then
        S=$SERIAL
    else
        # Android lists the Dot as "device", TWRP as "recovery".
        SERIALS=$(adb devices -l |
            awk '/ usb:/ && ($2 == "device" || $2 == "recovery") {print $1}')
        [ -n "$SERIALS" ] || die "no device attached over USB"
        [ "$(echo "$SERIALS" | wc -l)" = 1 ] ||
            die "several USB devices ($(echo $SERIALS)); set SERIAL"
        S=$SERIALS
    fi
else
    USB=
    S=$IP:$PORT
fi

a() { timeout 30 adb -s "$S" "$@"; }

# Connect, with a short timeout instead of adb wait-for-device. Over USB
# there is nothing to connect; the device only has to show up again.
connect() {
    for _ in 1 2 3 4 5 6; do
        [ -n "$USB" ] || timeout 10 adb connect "$S" >/dev/null 2>&1 || true
        if adb devices | awk -v s="$S" '$1 == s &&
                ($2 == "device" || $2 == "recovery") {f = 1} END {exit !f}'; then
            return 0
        fi
        sleep 2
    done
    die "$S does not answer over adb"
}

echo "== Connecting to $S"
connect

prop() { a shell getprop "$1" | tr -d '\r'; }

# Wait for TWRP after a reboot into recovery, with a bounded loop instead
# of adb wait-for-device.
wait_twrp() {
    for _ in $(seq 60); do
        sleep 2
        adb devices | awk -v s="$S" '$1 == s && $2 == "recovery" {f = 1}
            END {exit !f}' || continue
        [ -n "$(prop ro.twrp.version)" ] && return 0
    done
    die "TWRP did not come up within 2 minutes"
}

# TWRP's install writes only current-boot and current-system; check that
# they are the given slot's partitions.
check_current() {
    BYNAME=/dev/block/platform/bootdevice/by-name
    for part in boot system; do
        CUR=$(a shell readlink -f /dev/block/current-$part | tr -d '\r')
        WANT=$(a shell readlink -f $BYNAME/${part}_$1 | tr -d '\r')
        [ -n "$CUR" ] && [ "$CUR" = "$WANT" ] ||
            die "current-$part is '$CUR', not ${part}_$1 ('$WANT')"
    done
}

TWRP=$(prop ro.twrp.version)

if [ -n "$SLOT_WANTED" ]; then
    CUR_SLOT=$(prop ro.boot.slot_suffix)
    [ -n "$CUR_SLOT" ] || die "the device reports no slot; -s needs an A/B device"
    echo "   Current slot: ${CUR_SLOT#_}"
    if [ "$CUR_SLOT" = "_$SLOT_WANTED" ]; then
        echo "warning: slot $SLOT_WANTED is already the current slot;" \
            "the install overwrites the system that runs now" >&2
        # Ask the terminal, not stdin; without one, stop.
        ANSWER=
        printf 'Continue? [y/N] ' >&2
        read -r ANSWER 2>/dev/null < /dev/tty || echo >&2
        case "$ANSWER" in
            y|Y|yes|YES) ;;
            *) die "stopped; nothing was changed" ;;
        esac
    fi
    if [ -z "$TWRP" ] && [ -z "$USB" ]; then
        # TWRP has no Wi-Fi: the OpenRecoveryScripts switch the slot.
        [ "$CUR_SLOT" = "_$SLOT_WANTED" ] || SWITCH_IN_ORS=${CUR_SLOT#_}
    elif [ -z "$TWRP" ]; then
        if [ "$CUR_SLOT" = "_$SLOT_WANTED" ]; then
            echo "== Rebooting into TWRP"
        else
            echo "== Rebooting into TWRP to set slot $SLOT_WANTED"
        fi
        a reboot recovery || true
        wait_twrp
        TWRP=$(prop ro.twrp.version)
    fi
fi
if [ -n "$SLOT_WANTED" ] && [ -n "$TWRP" ]; then
    a shell command -v bcbtool >/dev/null ||
        die "TWRP $TWRP has no bcbtool; cannot switch the slot"
    OLD_SLOT=$(prop ro.boot.slot_suffix)
    if [ "$OLD_SLOT" != "_$SLOT_WANTED" ]; then
        SWITCHED_FROM=${OLD_SLOT#_}
        echo "== Setting slot $SLOT_WANTED active"
        a shell bcbtool set_active "$SLOT_WANTED" >/dev/null ||
            die "bcbtool set_active $SLOT_WANTED failed"
        a shell bcbtool dump | tr -d '\r' |
            grep -qE "Active slot: $SLOT_WANTED( |$)" ||
            die "bcbtool does not report slot $SLOT_WANTED as active"
        echo "== Rebooting TWRP onto slot $SLOT_WANTED"
        a reboot recovery || true
        wait_twrp
    fi
    [ "$(prop ro.boot.slot_suffix)" = "_$SLOT_WANTED" ] ||
        die "TWRP still runs on slot '$(prop ro.boot.slot_suffix)'"
fi

# TWRP's adbd already runs as root. In Android, adb root restarts adbd,
# which drops the connection.
if [ -z "$TWRP" ] && [ "$(a shell id -u | tr -d '\r')" != 0 ]; then
    a root >/dev/null 2>&1 || true
    sleep 3
    connect
    [ "$(a shell id -u | tr -d '\r')" = 0 ] || die "adb root failed"
fi

DEVICE=$(prop ro.product.device)
if [ -n "$FORCE" ]; then
    echo "   Device: $DEVICE (forced, not checked)"
else
    [ "$DEVICE" = biscuit ] || [ "$(prop ro.build.product)" = biscuit ] ||
        die "device is '$DEVICE', not biscuit (-f skips this check)"
fi
if [ -n "$TWRP" ]; then
    [ -n "$FORCE" ] || a shell test -x /sbin/amzn_bcbtool ||
        die "TWRP $TWRP has no /sbin/amzn_bcbtool; not amonet v2's TWRP"
    echo "   In TWRP $TWRP"
else
    echo "   Installed build: $(prop ro.build.date)"
fi
# Only A/B devices like Biscuit have a slot.
SLOT=$(prop ro.boot.slot_suffix)
[ -z "$SLOT" ] || echo "   Slot: $SLOT"
[ -z "$TWRP" ] || [ -z "$SLOT" ] || check_current "${SLOT#_}"

SIZE=$(wc -c < "$ZIP")
FREE=$(a shell df -k /data | tail -1 | awk '{print $4}' | tr -d '\r')
[ $((FREE * 1024)) -gt $((SIZE + 104857600)) ] ||
    die "not enough space on /data (${FREE} KiB free)"

echo "== Pushing $(basename "$ZIP") ($((SIZE / 1048576)) MiB)"
timeout 900 adb -s "$S" push "$ZIP" "$REMOTE" >/dev/null ||
    die "push failed"

echo "== Checking SHA-256"
LOCAL_SUM=$(sha256sum "$ZIP" | awk '{print $1}')
REMOTE_SUM=$(timeout 120 adb -s "$S" shell sha256sum "$REMOTE" |
    awk '{print $1}')
if [ "$LOCAL_SUM" != "$REMOTE_SUM" ]; then
    a shell rm -f "$REMOTE"
    die "checksum mismatch, removed $REMOTE"
fi

if [ -n "$TWRP" ]; then
    echo "== Installing in TWRP"
    LOG=$(timeout 900 adb -s "$S" shell twrp install "$REMOTE" 2>&1) ||
        FAILED=1
    echo "$LOG" | sed 's/^/   /'
    if [ -n "${FAILED:-}" ] ||
        echo "$LOG" | grep -qiE 'error installing|ended with error|abort'; then
        [ -z "${SWITCHED_FROM:-}" ] || echo "slot $SLOT_WANTED is active now;" \
            "adb shell bcbtool set_active $SWITCHED_FROM goes back" >&2
        die "install failed; the Dot stays in TWRP, $REMOTE is kept"
    fi
    a shell rm -f "$REMOTE"
    echo "== Rebooting"
    a reboot || true
    echo
    echo "Installed. When Android is back, check the build:"
    echo "  adb -s $S shell getprop ro.build.date"
    exit 0
fi

# TWRP moves the script to /tmp before it runs it line by line, so a cmd
# line can queue the next script for the next boot. cmd runs a shell
# command, so the zip is removed once it is installed.
TMP=$(mktemp -d)
trap 'rm -f "$TMP"/ors "$TMP"/stage2; rmdir "$TMP"' EXIT
STAGE2=/cache/recovery/biscuit-ota-stage2
if [ -n "${SWITCH_IN_ORS:-}" ]; then
    # Hide the zip unless TWRP runs on the wanted slot, so that install
    # fails instead of writing the old slot.
    cat > "$TMP"/stage2 <<EOF
cmd [ "\$(getprop ro.boot.slot_suffix)" = _$SLOT_WANTED ] || mv $REMOTE $REMOTE.wrong-slot
install $REMOTE
cmd rm -f $REMOTE
EOF
    cat > "$TMP"/ors <<EOF
cmd bcbtool set_active $SLOT_WANTED >/dev/null && bcbtool dump | grep -qE 'Active slot: $SLOT_WANTED( |\$)' && mv $STAGE2 $ORS
reboot recovery
EOF
else
    cat > "$TMP"/ors <<EOF
install $REMOTE
cmd rm -f $REMOTE
EOF
fi
echo "== Queueing the install in TWRP"
a shell mkdir -p /cache/recovery
a shell rm -f "$STAGE2"
if [ -n "${SWITCH_IN_ORS:-}" ]; then
    echo "   TWRP sets slot $SLOT_WANTED active and reboots into it first"
    a push "$TMP"/stage2 "$STAGE2" >/dev/null &&
        [ "$(a shell cat "$STAGE2" | tr -d '\r')" = "$(cat "$TMP"/stage2)" ] ||
        die "could not write $STAGE2"
fi
a push "$TMP"/ors "$ORS" >/dev/null &&
    [ "$(a shell cat "$ORS" | tr -d '\r')" = "$(cat "$TMP"/ors)" ] ||
    die "could not write $ORS"

echo "== Rebooting into recovery"
a reboot recovery || true

cat <<EOF

TWRP installs the zip, deletes it and reboots (cyan LED, then Android).
When Android is back, check that the installed build date is now the
new zip's:

EOF
[ -n "$USB" ] || echo "  adb connect $S"
echo "  adb -s $S shell getprop ro.build.date"
if [ -n "${SWITCH_IN_ORS:-}" ]; then
    cat <<EOF
  adb -s $S shell getprop ro.boot.slot_suffix   # _$SLOT_WANTED

If the Dot stays in TWRP (cyan LED), connect it over USB and look at
/tmp/recovery.log. If $REMOTE.wrong-slot exists,
TWRP did not come up on slot $SLOT_WANTED and nothing was installed.
To go back to slot $SWITCH_IN_ORS, run this in TWRP:
  adb shell bcbtool set_active $SWITCH_IN_ORS
EOF
fi
