# Biscuit boot chain: two slots and boot-only flashing

Running LineageOS 18.1's arm64 4.9 kernel (`lineage-18.1`) on slot B next
to a 3.18 build on slot A needs amonet v2.0.1 or newer. Its kaeru
(`2.0.0-gab93750`) passes LK's active slot on
([kaeru #102](https://github.com/R0rt1z2/kaeru/issues/102), fixed in
[`ab93750f`](https://github.com/R0rt1z2/kaeru/commit/ab93750f2475a8ca7e51a82168634f48d62e3cfa)).
amonet v2.0.0's kaeru loads `boot_a` whatever slot is active, so slot A's
kernel starts against `system_b` when slot B is active.

## 0. Enter TWRP and check it

From Android: `adb reboot recovery`. If Android does not boot: hold only
**Volume Up** while connecting power (kaeru's recovery key; Volume Down alone
is fastboot, MUTE alone is Preloader USBDL). TWRP shows the pulsing cyan LED.

```sh
adb devices -l
adb shell 'command -v twrp; getprop ro.twrp.version; getprop ro.boot.slot_suffix'
adb shell bcbtool dump
```

Expect TWRP `3.7.0_9-0`, slot `_a`, and `Active slot: a`.

## 1. Update amonet from v2.0.0

Install `amonet-biscuit-v2.0.1.zip` from TWRP, as amonet's thread says:

```sh
adb push amonet-biscuit-v2.0.1.zip /tmp/
adb shell twrp install /tmp/amonet-biscuit-v2.0.1.zip
```

It rewrites preloader, LK, TZ, the payload, kaeru (`expdb`) and TWRP, and
wipes `misc`, which holds the slot selection, then reboots into TWRP. Check
the active slot there with `adb shell bcbtool dump` before booting Android.

## 2. Put 4.9 on slot B, keep 3.18 on slot A

```sh
adb reboot recovery
adb shell bcbtool set_active b
adb shell bcbtool dump              # Active slot: b
adb reboot recovery
adb shell 'getprop ro.boot.slot_suffix; readlink -f /dev/block/current-boot /dev/block/current-system'
```

Continue only if TWRP now reports `_b` and `current-boot`/`current-system`
resolve to the same devices as
`readlink -f /dev/block/platform/bootdevice/by-name/boot_b` and `.../system_b`.
Then inspect the 4.9 OTA's updater as usual and sideload it:

```sh
adb shell twrp sideload
adb sideload lineage-18.1-*-biscuit.zip      # the 4.9 build
adb reboot
```

Check it with:

```sh
adb shell 'uname -r; getprop ro.boot.slot_suffix'   # 4.9.337-…, _b
```

To switch between the two, set the slot from TWRP and reboot:

```sh
adb shell bcbtool set_active a     # 3.18; set_active b for 4.9
adb reboot
```

`tools/biscuit-ota.sh -s <a|b>` does the switch and the install from
Android, over Wi-Fi too (see the device [README](../../README.md)).

## Flashing only a new boot.img

For a kernel-only change, package the build's `boot.img` and install it from
TWRP with slot B (4.9) active. `make-flash-zip.py` writes a TWRP zip that
checks the image's sha256, writes only `/dev/block/current-boot` after
checking that it is `boot_<active slot>`, backs the old one up to
`/tmp/boot-backup.img` first and reads it back:

```sh
device/amazon/biscuit/tools/bootchain/make-flash-zip.py boot out/target/product/biscuit/boot.img biscuit-boot.zip
adb shell 'bcbtool dump; readlink -f /dev/block/current-boot'   # slot b, mmcblk0p11
adb push biscuit-boot.zip /tmp/
adb shell twrp install /tmp/biscuit-boot.zip
```

The kernel's version string changes with every commit, so the Wi-Fi and
Bluetooth modules in system from an older build no longer load; sideload the
full OTA when the test needs them.

## Going back

amonet's zip restores its whole boot chain from TWRP; without a working
TWRP, use amonet's Preloader USBDL recovery (MUTE while plugging in, then
`bootrom-step.sh` from the amonet zip).
