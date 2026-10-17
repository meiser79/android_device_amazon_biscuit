# Android device tree for the Amazon Echo Dot (2nd gen) (`biscuit`)

```
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
```

LineageOS 18.1 for Biscuit (MT8163, 512 MB RAM, no display), built on the
maintained [amazon-oss](https://github.com/amazon-oss) MT8163 common tree and
the 32-bit Fire OS 6 Linux 3.18 kernel. Biscuit needs the amonet v2 unlock
(native `boot_a/b`, `system_a/b` GPT and TWRP in `recovery`).

## Sources

Add to `.repo/local_manifests/biscuit.xml` on top of a LineageOS 18.1 checkout:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote name="amazon-oss" fetch="https://github.com/amazon-oss" revision="refs/heads/lineage-18.1" />
  <remote name="biscuit" fetch="https://github.com/meiser79" revision="refs/heads/lineage-18.1" />
  <remote name="mt8163" fetch="https://github.com/mt8163" />

  <project path="device/amazon/biscuit" name="android_device_amazon_biscuit" remote="biscuit" />
  <project path="kernel/amazon/biscuit" name="android_kernel_amazon_biscuit" remote="biscuit" />
  <project path="vendor/amazon/biscuit" name="android_vendor_amazon_biscuit" remote="biscuit" />

  <project path="device/amazon/mt8163-common" name="android_device_amazon_mt8163-common" remote="amazon-oss" />
  <project path="vendor/amazon/mt8163-common" name="android_vendor_amazon_mt8163-common" remote="amazon-oss" />
  <project path="hardware/amazon" name="android_hardware_amazon" remote="amazon-oss" />
  <project path="hardware/mediatek/mt66xx" name="android_hardware_mediatek_mt66xx" remote="amazon-oss" />
  <project path="patches" name="patches" remote="amazon-oss" />
</manifest>
```

Then sync and apply the amazon-oss source patches, then Biscuit's own
(`patches/<project path>/`, applied with `git am`, skipping patches that are
already in):

```sh
repo sync -c --no-clone-bundle --no-tags
./patches/apply.sh
./device/amazon/biscuit/patches/apply.sh
```

| Biscuit patch | Why |
| --- | --- |
| `system/bt` `0001-hci-Tolerate-a-rejected-extended-features-page` | The MT8163 controller rejects a classic feature page it reports (status 0x30); Android 11 aborts on it, Android 7 ignored it |

## Proprietary files

Common blobs come from the karnak Fire OS 7 image listed in
`device/amazon/mt8163-common/proprietary-files.txt`. Biscuit's own blobs
(connsys loader/launcher and firmware) come from Biscuit's stock Fire OS
6.5.7.4 system image:

```sh
cd device/amazon/biscuit
./extract-files.sh --only-common <karnak system dump>
./extract-files.sh --only-target <biscuit 6.5.7.4 system dump>
```

## Build

```sh
. build/envsetup.sh
breakfast lineage_biscuit-userdebug
mka bacon
```

## Install

Only through amonet v2 TWRP sideload. The OTA writes nothing except TWRP's
`/dev/block/current-system` and `/dev/block/current-boot` (see
`releasetools/releasetools.py`, which also refuses incremental and two-step
packages and checks the active slot against amonet's BCB). Inspect the
updater script before every install, and install twice to fill both slots.

```sh
adb shell 'getprop ro.twrp.version; getprop ro.boot.slot_suffix'
adb shell twrp sideload
adb sideload lineage-18.1-*-biscuit.zip
```

The OTA writes `boot.img` before it restores addon.d scripts, so Magisk's
addon.d re-patches the new boot image, and then waits (at most 120 s) for
addon.d jobs that continue in the background, as Magisk's does: once the
install reports success, the boot image is final.

## How Biscuit differs from the other MT8163 Echo devices

- **Kernel** (`kernel/amazon/biscuit`, branch `lineage-18.1`): the
  Fire OS 6.5.7.1 3.18 source that CM14 builds, booted as a 32-bit
  `zImage-dtb` with the Fire OS 6 layout (kernel `0x40008000`, ramdisk
  `0x44000000`, `bootopt=64S3,32N2,32N2`). No arm64 kernel has booted on
  Biscuit yet, although amonet v2's kaeru can switch LK to 64-bit kernels;
  the arm64 4.9 port is on the `lineage-18.1-4.9` branches of the device and
  kernel repositories. The userspace is 32-bit either way.
  On top of CM14's kernel patches it carries the AOSP android-3.18 binder
  (`binder`/`hwbinder`/`vndbinder`, 64-bit protocol). The kernel forces its
  command line (the Fire OS LK passes `root=`, `dm=` and a stale slot suffix),
  keeps only the LK serial number and boot reason, and exports amonet v2's
  active slot (`misc+0x360`) as `androidboot.slot_suffix`, so first-stage init
  mounts the right `system_a/b`. It builds with the tree's ARM GCC 4.9.
- **Radio**: the 3.18 kernel has MT8163 connsys built in (no `mt66xx`
  modules); Biscuit's stock `wmt_loader`/`wmt_launcher` bring it up, then
  `libbt-vendor`, `libwifi-hal-mt66xx` and `lib_driver_cmd_mt66xx` from
  `hardware/mediatek/mt66xx` use `/dev/stpbt` and `/dev/wmtWifi`.
- **USB**: 3.18 only has the `android_usb` gadget, so `init.target.rc`
  switches mt8163-common's configfs setup back to `sys.usb.configfs=0`;
  AOSP's `init.usb.rc` then drives the gadget and adbd uses FunctionFS.
- **Display**: none. `hwcomposer.biscuit` is a headless HWC 1.1 with one
  1x1 primary display and software vsync at 1 Hz (`debug.biscuit.hwc.size=WxH`
  and `debug.biscuit.hwc.fps=1..60` override them for debugging), wrapped by the
  composer 2.1 passthrough service. It claims every layer as an overlay and
  drops it, so SurfaceFlinger never composes and never needs a framebuffer
  target buffer (there is no fbdev to allocate it from). mt8163-common's
  LiveDisplay HAL is left out: without the MediaTek display driver it never
  registers, and its VINTF declaration made `system_server` wait for it
  until Watchdog restarted it. The stock HWC is 1.0, which
  `HWC2On1Adapter` rejects. The kernel has no GPU driver, so GLES is
  SwiftShader on AOSP's `gralloc.default` (`ro.hardware.egl`,
  `ro.hardware.gralloc`), as on CM14. `gralloc.default` has no BLOB buffers, so Codec2
  takes its linear buffers from ION (`debug.stagefright.c2-poolmask` in
  `vendor.prop`, over mt8163-common's mask).
- **Recovery**: `recovery` holds amonet's TWRP. `init.target.rc` bind-mounts
  `/system/bin/true` over `install-recovery.sh`, so Android never replaces it.

## Headless features

- **Audio**: `audio.primary.biscuit` runs the Fire OS 6 (Android 7.1)
  `audio.primary_amazon.mt8163.so` under the Android 11 HAL ABI and switches
  the TLV320DAC3203 speaker route and amplifier around playback. Missing
  legacy symbols come from `libbiscuit_audio_shim` and the amazon-oss shims
  (`blob_fixup` in `extract-files.sh`). The HAL also drives GPIO87; its 3.18
  sysfs path is rewritten to `/dev/biscuit_mt_gpio`. Its Biscuit mixer setup
  comes from Fire OS's `/system/etc/audio_device.xml` (the four TI ADC mic
  inputs, mic PGA 20 dB, SPI framing, playback ramp); without it the
  microphones stay muted. Amazon's signal processing (`libasp`: beamforming,
  AEC, AGC) runs with its models from `/system/vendor/etc/audio-algorithms`.
  Its LIPC client (Amazon's IPC over D-Bus, which Fire OS runs and LineageOS
  does not) talks to a source-built `liblipc` stub (`lipc/`): every call
  succeeds and no events arrive, as with the Echo Show's Fire OS 7 `libasp`,
  which has no LIPC at all. Its playback path also applies Amazon's device volume
  (`music_volTab`), which Fire OS sets over LIPC; the HAL wrapper sets it to
  the 0 dB top (`persist.vendor.biscuit.asp_volume`, default 30, -1 to leave
  it alone), so Android's volume alone controls loudness. Microphone and voice
  recognition capture get the AOSP/WebRTC AGC (`configs/audio/audio_effects.xml`),
  as on CM14.
- **Bluetooth speaker**: A2DP sink and AVRCP controller only; BiscuitService
  answers pairing requests (PIN `0000` or just-works) and connects A2DP sink
  after bonding.
- **Microphone privacy**: as on Fire OS, the kernel owns it. `amz_priv`
  mutes GPIO87 (global GPIO 444) when the microphone button is
  released and unmutes it on the next press, even if Android is hung.
  Software can enter privacy (`privacy_trigger`) but never leave it: the HAL
  wrapper refuses an unmute while `privacy_state` is 1, so
  `biscuit_service mic unmute` only works after the button. `biscuit-privacyd`
  follows `privacy_state` into Android's microphone mute flag. Both live
  under `/dev/biscuit_privacy` (the keypad's `amz_privacy` sysfs group).
- **LED ring**: `biscuit-ledd` drives the IS31FL3236 ring, and BiscuitService
  mirrors the music volume on it.
- **Ambient light**: `sensors.biscuit` (sensors HAL 1.3) reports the
  TSL2584TSV through IIO `illuminance0_input` or, on boards with a TSL2540,
  its i2c0 sysfs `als_lux` (powered through `als_power_state`).
- **Memory**: KSM merges identical pages (CM14's settings), and the boot
  animation is off (`debug.sf.nobootanimation=1`).
- **Debugging**: `/dev/i2c-*` (for `i2c-poke`), `/sys/class/gpio`, and
  `tinymix`/`tinyplay`/`tinycap`/`tinypcminfo` on userdebug builds.
- **CLI**: `/system/bin/biscuit_service` (`volume`, `mute`/`mic`,
  `countdown`, `bt pair|off`, `wifi on|off|connect`), unchanged from CM14.
- **Wi-Fi** runs as STA only; the connsys WMT/PSM/BTIF threads run SCHED_FIFO.
- **Apps**: `BiscuitEmptyLauncher` is a black HOME activity (Android needs one
  to finish booting). Its `overrides` drop the launchers, setup wizard, updater,
  user apps, keyboard, wallpapers, themes and printing from the image.

## Status

Verified on the host:

- The kernel builds with the tree's ARM GCC 4.9 into a `zImage-dtb` with
  the same `evt`/`min_evt` DTBs as amonet's working TWRP kernel.
- Host tests pass for the bootargs allowlist, amonet slot parsing, the OTA
  write-target checks, the headless HWC (Android 11 headers),
  `biscuit-privacyd` and `sensors.biscuit` (both light sensor sources,
  ASan/UBSan).
- The audio wrapper compiles for arm32 against Android 11 headers with its
  struct-layout asserts; the shim exports exactly the symbols the blobs
  import; BiscuitService compiles against the Android 11 API signatures it
  uses.
- `extract-files.sh --only-target` extracts all 48 blobs and applies every
  fixup.

Verified on the device: Android 11 boots on the 3.18 kernel (first-stage
mount, binder, SwiftShader, adb over `android_usb`), and `system.img` fits
the 768 MiB `system` partition (the build enforces
`BOARD_SYSTEMIMAGE_PARTITION_SIZE`).

Replaced: CM14's Amazon AEC preload (`amznaec`), by Amazon's ASP processing
in the vendor HAL. Not ported: CM14's hostname patch, which targets Android
7's `ConnectivityService`.
