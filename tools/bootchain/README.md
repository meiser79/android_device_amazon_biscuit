# Biscuit boot chain: kaeru with the A/B fix

An add-on to amonet v2 for running LineageOS 18.1's arm64 4.9 kernel
(`lineage-18.1`) on slot B next to the working 3.18 build on slot A:

- **kaeru that boots slot B.** amonet v2.0.0's kaeru always loads `boot_a`
  ([kaeru #102](https://github.com/R0rt1z2/kaeru/issues/102)). The fix is
  merged upstream as
  [`ab93750f`](https://github.com/R0rt1z2/kaeru/commit/ab93750f2475a8ca7e51a82168634f48d62e3cfa)
  (from [kaeru #112](https://github.com/R0rt1z2/kaeru/pull/112)); this build
  uses it on [`meiser79/kaeru`](https://github.com/meiser79/kaeru) branch
  `biscuit-7673aa2-issue102`, on the commit amonet's kaeru reports
  (`2.0.0-g7673aa2-dirty`). Without it, slot
  A's kernel starts against `system_b` when slot B is active, so keep ROMs on
  slot A or install this kaeru first. Tested on the device in both
  directions: slot B boots 4.9, slot A boots 3.18.

## Build

```sh
tools/bootchain/build.sh /path/to/amonet-biscuit-v2.0.0.zip
```

Every input is pinned: the amonet zip and its `twrp.img`/`biscuit-kaeru.bin`
and the Fire OS 6.5.7.4 OTA's `lk` by sha256, kaeru by commit. kaeru builds
in its own Docker image from that `lk`. It works in `$BOOTCHAIN_WORK`
(default `./bootchain-work`, which also caches the OTA); output goes to its
`out/`. Needs git, python3, curl and Docker.

## Install

Files in `bootchain-work/out` (check them first: `sha256sum -c SHA256SUMS`):

| File | Writes | What it is |
| --- | --- | --- |
| `biscuit-kaeru-issue102.zip` | `expdb` | kaeru `7673aa2` (the commit amonet v2.0.0's kaeru reports, `2.0.0-g7673aa2-dirty`) plus the fix for [kaeru #102](https://github.com/R0rt1z2/kaeru/issues/102), built with kaeru's Docker image from the stock NS6574 `lk.img` (`20221007_072309`, sha256 `a993322d…`) |
| `restore-amonet-kaeru.zip` | `expdb` | amonet v2.0.0's original `biscuit-kaeru.bin` |
| `restore-amonet-twrp.zip` | `recovery` | amonet v2.0.0's original `twrp.img` |

Every zip checks its image's sha256, refuses any partition other than
`recovery`, `expdb` or the active slot's `boot`, saves the current partition
to `/tmp/<part>-backup.img` (and `/sdcard/` when data is mounted), writes it,
and reads it back.

### 0. Enter TWRP and check it

From Android: `adb reboot recovery`. If Android does not boot: hold only
**Volume Up** while connecting power (kaeru's recovery key; Volume Down alone
is fastboot, MUTE alone is Preloader USBDL). TWRP shows the pulsing cyan LED.

```sh
adb devices -l
adb shell 'command -v twrp; getprop ro.twrp.version; getprop ro.boot.slot_suffix'
adb shell bcbtool dump
```

Expect TWRP `3.7.0_9-0`, slot `_a`, and `Active slot: a`.

### 1. Install kaeru with the A/B fix

Without this fix, LK boots `boot_a` whatever slot is active (kaeru #102), so
slot B cannot hold a second kernel.

```sh
adb push biscuit-kaeru-issue102.zip /tmp/
adb shell twrp install /tmp/biscuit-kaeru-issue102.zip
adb pull /tmp/expdb-backup.img
adb pull /tmp/expdb-backup.img.sha256
adb reboot
```

Android (slot A, 3.18) must boot as before. Then check:

```sh
adb shell cat /proc/cmdline | tr ' ' '\n' | grep -E 'slot_suffix|lk_build_desc'
```

If the Dot no longer starts (no TWRP, no Android, preloader loop): use
amonet's Preloader USBDL recovery (MUTE while plugging in, then
`bootrom-step.sh` from the amonet v2.0.0 zip); it writes amonet's original
kaeru back to `expdb`, together with LK, TZ, TWRP and the payload. From a
working TWRP, `restore-amonet-kaeru.zip` does the same without USBDL.

### 2. Put 4.9 on slot B, keep 3.18 on slot A

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

### Flashing only a new boot.img

For a kernel-only change, package the build's `boot.img` and install it from
TWRP with slot B (4.9) active. It writes only `/dev/block/current-boot`, after
checking that it is `boot_<active slot>`, and backs the old one up first:

```sh
device/amazon/biscuit/tools/bootchain/make-flash-zip.py boot out/target/product/biscuit/boot.img biscuit-boot.zip
adb shell 'bcbtool dump; readlink -f /dev/block/current-boot'   # slot b, mmcblk0p11
adb push biscuit-boot.zip /tmp/
adb shell twrp install /tmp/biscuit-boot.zip
```

The kernel's version string changes with every commit, so the Wi-Fi and
Bluetooth modules in system from an older build no longer load; sideload the
full OTA when the test needs them.

### Going back completely

From TWRP: `restore-amonet-kaeru.zip` and `restore-amonet-twrp.zip` (or the
pulled `*-backup.img` files), or amonet's Preloader USBDL `bootrom-step.sh`.
