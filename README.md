# Android device tree for the Amazon Echo Dot (2nd gen) (`biscuit`)

```
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
```

LineageOS 18.1 for Biscuit (MT8163, 512 MB RAM, no display), built on the
maintained [amazon-oss](https://github.com/amazon-oss) MT8163 common tree and
its Linux 4.9 kernel. Biscuit needs the amonet v2 unlock (native `boot_a/b`,
`system_a/b` GPT and TWRP in `recovery`).

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
  <project path="kernel/amazon/mt66xx-common" name="android_kernel_amazon_mt66xx-common" remote="amazon-oss" />
  <project path="kernel/amazon/mt66xx-wifi" name="android_kernel_amazon_mt66xx-wifi" remote="amazon-oss" />
  <project path="kernel/amazon/mt66xx-bt" name="android_kernel_amazon_mt66xx-bt" remote="amazon-oss" />
  <project path="patches" name="patches" remote="amazon-oss" />

  <!-- aarch64-linux-gnu GCC for the 4.9 kernel (TARGET_KERNEL_CROSS_COMPILE_PREFIX in mt8163-common) -->
  <project path="prebuilts/linaro" name="android_prebuilts_linaro" remote="mt8163" revision="refs/heads/lineage-20.0" clone-depth="1" />
</manifest>
```

Then sync and apply the amazon-oss source patches:

```sh
repo sync -c --no-clone-bundle --no-tags
./patches/apply.sh
```

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

## How Biscuit differs from the other MT8163 Echo devices

- **Kernel** (`kernel/amazon/biscuit`): amazon-oss `lineage-18.1` 4.9 plus
  `biscuit_defconfig` and a Biscuit DTS ported from the Fire OS 6.5.7.1 3.18
  source. The kernel forces its command line (the Fire OS LK passes `root=`,
  `dm=` and a stale slot suffix), keeps only the LK serial number and boot
  reason, and exports amonet v2's active slot (`misc+0x360`) as
  `androidboot.slot_suffix`, so first-stage init mounts the right
  `system_a/b`.
- **Radio**: MT8163 connsys like douglas/karnak: `mt66xx` kernel modules,
  `libbt-vendor`, `libwifi-hal-mt66xx` and `lib_driver_cmd_mt66xx` from
  `hardware/mediatek/mt66xx`, and Biscuit's stock `wmt_loader`/`wmt_launcher`.
- **Display**: none. `hwcomposer.biscuit` is a headless HWC 1.1 (software
  vsync, size from `debug.biscuit.hwc.size`, default 320x320) wrapped by the
  composer 2.1 passthrough service. The stock HWC is 1.0, which
  `HWC2On1Adapter` rejects.
- **Recovery**: `recovery` holds amonet's TWRP. `init.target.rc` bind-mounts
  `/system/bin/true` over `install-recovery.sh`, so Android never replaces it.

## Status

Verified on the host:

- The kernel builds (`Image.gz-dtb`), and the `mt66xx` common, Wi-Fi and BT
  modules build against it.
- Host tests pass for the bootargs allowlist, amonet slot parsing and the OTA
  write-target checks.

Not verified yet (needs a LineageOS 18.1 build and the device):

- Biscuit's LK booting an arm64 kernel (`bootopt=64S3,32N2,64N2` from
  mt8163-common).
- Mali GPU (Fire OS 6 disables it on Biscuit).
- Fire OS 6 `wmt_loader`/`wmt_launcher` and `WIFI_RAM_CODE_8163` with the
  Fire OS 7-era `mt66xx` modules. Fallback: the same files from the karnak dump.
- `system.img` size against the 768 MiB `system` partition.

Not ported yet: audio (TLV320 ADC/DAC through `audio.primary.amazon_wrapper`),
the LED ring daemon, the `amz_priv` mic-privacy driver and the CM14 connsys
RT-priority tuning.
