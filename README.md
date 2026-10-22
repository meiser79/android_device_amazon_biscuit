# Android device tree for the Amazon Echo Dot (2nd gen) (`biscuit`)

```
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
```

LineageOS 18.1 for Biscuit (MT8163, 512 MB RAM, no display), built on the
maintained [amazon-oss](https://github.com/amazon-oss) MT8163 common tree and
the arm64 Linux 4.9 kernel. This `lineage-18.1` branch is the 4.9 port:
it boots Android 11 with Wi-Fi, Bluetooth, the speaker and the microphone.
`lineage-18.1-3.18` is the same device tree on the 32-bit Fire OS 6 3.18
kernel.
Biscuit needs the amonet v2 unlock (native `boot_a/b`, `system_a/b` GPT and
TWRP in `recovery`). To keep a 3.18 build on slot A and this one on slot B,
use amonet v2.0.1 or newer: its kaeru boots the active slot, while v2.0.0's
boots `boot_a` whatever slot is active. On v2.0.0, install
`amonet-biscuit-v2.0.1.zip` from TWRP; it also wipes `misc`, which holds
the slot selection, so check `adb shell bcbtool dump` in the TWRP it
reboots into. [`tools/bootchain/`](tools/bootchain/README.md) covers the
slot switch and flashing only a `boot.img`.

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
| `external/openssh` `0001-sshd-Tolerate-a-NULL-privsep-pw_passwd` | bionic returns the privsep user (`shell`) with a NULL `pw_passwd`; sshd crashed in `strlen()` on it at every start as root |
| `external/openssh` `0002-start-ssh-Use-the-sshd_config-the-build-installs` | `start-ssh` ran sshd with `/vendor/etc/ssh/sshd_config`, but the config installs to `/system/etc/ssh` only |
| `external/openssh` `0003-config.h-Enable-SHA-384-and-SHA-512` | The prebuilt `config.h` lacks `HAVE_EVP_SHA384`/`HAVE_EVP_SHA512`, so every SHA-512 digest failed and sshd could not load its host keys ("Unable to shield host key") |
| `frameworks/av` `0001-TimeCheck-Report-a-timeout-once-instead-of-wedging-a` | amazon-oss keeps audioserver alive on a TimeCheck timeout, but the expired check then fired every second with its lock held, so one binder call over 5 s left audioserver hung for good (no recording, a HAL tombstone every second); each timeout is now logged once and dropped |
| `frameworks/base` `0001-PhoneWindowManager-Hand-claimed-volume-keys-to-Biscu` | Kiosk Satellite claims the volume buttons through BiscuitService to steer a Music Assistant group; while `sys.biscuit.volume_claimed` is 1, each press goes to BiscuitService instead of changing the volume |
| `frameworks/base` `0002-PhoneWindowManager-Skip-the-torch-callback-without-a` | Lineage's power button torch registered a torch callback at boot, and without cameraserver CameraManager retried the camera service every second for it; the callback is now registered only when a camera flash is declared |
| `frameworks/opt/net/wifi` `0001-ClientModeImpl-Keep-power-save-off-after-DHCP-while-` | After every DHCP exchange, lease renewals included, Android turned power save back on without asking `WifiLockManager`, so a `WIFI_MODE_FULL_HIGH_PERF` lock held for good (Kiosk Satellite's) lost its effect at the first renewal (measured: lock held, `iw` showed power save on) |
| `system/bt` `0001-hci-Tolerate-a-rejected-extended-features-page` | The MT8163 controller rejects a classic feature page it reports (status 0x30); Android 11 aborts on it, Android 7 ignored it |
| `kernel/amazon/mt8163-4.9` `0001-init-Drop-the-skip_initramfs-parameter` | For the other MT8163 devices, which build mt8163-common's kernel (Biscuit's kernel already has it): none boots with `skip_initramfs`, but the string makes Magisk take the device for legacy system-as-root and hex-patch the kernel image, which kept Biscuit's repacked 3.18 kernel from starting |
| `device/amazon/mt8163-common` `0001-mt8163-releasetools-Let-Magisk-s-addon.d-patch-the-f` | For the other MT8163 devices (Biscuit's own releasetools already does it): the generic updater restores addon.d before it writes `boot.img`, so Magisk's background re-patch raced the boot write and could still be writing after the install reported success; `boot.img` now goes first and the updater waits for addon.d's job |
| `hardware/amazon` `0001-audio-hal-Capture-mic-sources-on-Amazon-s-ASR-path` | For the other MT8163 devices (Biscuit's own HAL wrapper already does it): amazon-oss moved mic and camcorder captures to libasp's voice pipeline, which runs AEC, NR and AGC for calls but no beamformer; they now use the ASR path, which Fire OS uses for Alexa (AEC and beamformer over all calibrated mics, no NR or AGC). Checked on the Show 8 (`PipelineAsr`), the Spot and the Show 5. `persist.vendor.amazon.mic_source` picks another source (7 for the voice pipeline, -1 for the app's) |
| `device/amazon/mt8163-common` `0002-mt8163-audio-Add-the-WebRTC-AGC-to-mic-captures` | For the other MT8163 devices (Biscuit's own `audio_effects.xml` already has it): the ASR path has no AGC, so AOSP's WebRTC AGC from `libaudiopreprocessing` now runs on `mic` and `voice_recognition` |
| `kernel/amazon/mt66xx-common` `0001-wmt-Request-firmware-by-its-base-name` | Fire OS 6's `wmt_launcher` passes the connsys ROM patch as an absolute path, which `request_firmware()` cannot resolve below `firmware_class.path`; the driver retried forever, so connsys never came up (no `wlan0`, the Bluetooth HAL blocked on `/dev/stpbt`) |
| `system/netd` `0001-MDnsSdListener-Report-start-service-OK-when-mdnsd-al` | libmdnssd starts `mdnsd` for native clients (Kiosk Satellite's sendspin), so NsdService's `start-service` found it running and logged a `NativeDaemonFailureException` ("402 Service already running") although discovery works |

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
updater script before every install, and install twice to fill both slots,
or once into slot B next to a 3.18 build on slot A
([`tools/bootchain/README.md`](tools/bootchain/README.md) covers the
amonet update, the slot switch and boot-only flashing).

```sh
adb shell 'getprop ro.twrp.version; getprop ro.boot.slot_suffix'
adb shell twrp sideload
adb sideload lineage-18.1-*-biscuit.zip
```

The OTA writes `boot.img` before it restores addon.d scripts, so Magisk's
addon.d re-patches the new boot image, and then waits (at most 120 s) for
addon.d jobs that continue in the background, as Magisk's does: once the
install reports success, the boot image is final.

### Updating from Android

TWRP runs an OpenRecoveryScript, so a build that is already installed can
be updated from Android, over Wi-Fi or USB: `tools/biscuit-ota.sh` pushes
the zip over adb, checks its SHA-256 on the device, writes
`install /sdcard/update.zip` to `/cache/recovery/openrecoveryscript` and
reboots into recovery. TWRP installs it to the current slot, deletes the
zip (`cmd rm`) and reboots; the other slot keeps the previous build.

```sh
tools/biscuit-ota.sh 192.168.1.50 lineage-18.1-*-biscuit.zip   # Wi-Fi
tools/biscuit-ota.sh usb lineage-18.1-*-biscuit.zip            # USB
```

With several USB devices attached, `SERIAL=<serial>` picks one. If the Dot
is already in TWRP (USB only: TWRP has no Wi-Fi), the script installs the
zip right away with `twrp install` and reboots into Android; if TWRP
reports an error, the Dot stays in TWRP. `-f` (or `FORCE=1`) skips the
checks for a Biscuit and for amonet v2's TWRP, so the script also updates
other Echo devices whose TWRP runs an OpenRecoveryScript; the zip's updater
still decides whether it installs.

`-r` (or `RESET=1`) factory resets the Dot after the install, with TWRP's
`wipe data`: it wipes data and cache but keeps `/sdcard`. A failed install
stops TWRP's script, so nothing is wiped then. The reset also forgets the
Wi-Fi network, so afterwards connect USB and set it up again with
`adb shell biscuit_service wifi connect '<ssid>' '<psk>'`.

`-s <a|b>` (or `SLOT=`) installs into that slot instead, over Wi-Fi or
USB. The script prints the current slot and asks before reinstalling into
it. TWRP links `/dev/block/current-boot` and `current-system` to the slot
it booted on, so it has to boot on the wanted slot first:

- Over USB, the script sets the slot active with `bcbtool` in TWRP,
  reboots into recovery and checks the links before installing. If the
  install fails after a switch, it prints the `bcbtool set_active` command
  that returns to the old slot.
- Over Wi-Fi, it queues two OpenRecoveryScripts: the first sets the slot
  active and reboots into recovery, the second checks that TWRP runs on
  that slot, installs and reboots. If the Dot stays in TWRP (cyan LED),
  connect USB, read `/tmp/recovery.log`, and
  `adb shell bcbtool set_active <old slot>` goes back.

## How Biscuit differs from the other MT8163 Echo devices

- **Kernel** (`kernel/amazon/biscuit`, branch `lineage-18.1`): the
  amazon-oss MT8163 4.9 kernel with Biscuit's device tree and the Fire OS
  drivers it needs (`amz_priv`, the TSL2540/TSL2584 light sensors, the
  IS31FL3236 LED ring), built by mt8163-common's Linaro GCC into an arm64
  `Image.gz-dtb` (`mediatek/biscuit_evt`). Stock LK forces 32-bit kernels;
  amonet v2's kaeru switches it to 64-bit for mt8163-common's
  `bootopt=64S3,32N2,64N2`. The layout is mt8163-common's (kernel
  `0x40080000`, second `0x40f00000`, tags `0x48000000`) with the ramdisk at
  the stock `0x44000000`, inside 512 MB. Fire OS 6's LK reports 504 MiB,
  and the DT also reserves the top 8 MiB (`0x5f800000`), ATF/TEE secure
  memory the kernel hung on right after `paging_init`. Biscuit takes the
  codec MCLK from the camera MCLK and builds without
  `CONFIG_SND_I2S_MCLK`, so its audio node keeps mt8163.dtsi's clock list,
  and DL1 starts the AFE itself. The userspace stays 32-bit. The
  kernel forces its command line (the Fire OS LK passes `root=`, `dm=` and a
  stale slot suffix), keeps only the LK serial number and boot reason, and
  exports amonet v2's active slot (`misc+0x360`) as `androidboot.slot_suffix`,
  so first-stage init mounts the right `system_a/b`. It writes its console to
  pstore (`0x44410000`, console zone at `0x444bf000`).
- **Radio**: MT8163 connsys comes from the `mt66xx` modules
  (`init.insmod.cfg`); Biscuit's stock `wmt_loader`/`wmt_launcher` bring it
  up (the WMT driver requests the ROM patch by its base name, see the patch
  table), then
  `libbt-vendor`, `libwifi-hal-mt66xx` and `lib_driver_cmd_mt66xx` from
  `hardware/mediatek/mt66xx` use `/dev/stpbt` and `/dev/wmtWifi`.
- **USB**: mt8163-common's configfs gadget, unchanged.
- **Display**: none. `hwcomposer.biscuit` is a headless HWC 1.1 with one
  1x1 primary display and software vsync at 1 Hz (`debug.biscuit.hwc.size=WxH`
  and `debug.biscuit.hwc.fps=1..60` override them for debugging), wrapped by the
  composer 2.1 passthrough service. It claims every layer as an overlay and
  drops it, so SurfaceFlinger never composes and never needs a framebuffer
  target buffer (there is no fbdev to allocate it from). mt8163-common's
  LiveDisplay HAL is left out: without the MediaTek display driver it never
  registers, and its VINTF declaration made `system_server` wait for it
  until Watchdog restarted it. The stock HWC is 1.0, which
  `HWC2On1Adapter` rejects. Mali stays off on Biscuit, so GLES is
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
  sysfs path is rewritten to `/dev/biscuit_mt_gpio`, which links to the 4.9
  pinctrl node. Its Biscuit mixer setup
  comes from Fire OS's `/system/etc/audio_device.xml` (the four TI ADCs
  behind the seven microphones, mic PGA 20 dB, SPI framing, playback ramp);
  without it the microphones stay muted. Amazon's signal processing
  (`libasp`: beamforming, AEC, AGC) runs with its models from
  `/system/vendor/etc/audio-algorithms`.
  Its LIPC client (Amazon's IPC over D-Bus, which Fire OS runs and LineageOS
  does not) talks to a source-built `liblipc` stub (`lipc/`): every call
  succeeds and no events arrive, as with the Echo Show's Fire OS 7 `libasp`,
  which has no LIPC at all. Its playback path also applies Amazon's device volume
  (`music_volTab`), which Fire OS sets over LIPC; the HAL wrapper sets it to
  the 0 dB top (`persist.vendor.biscuit.asp_volume`, default 30, -1 to leave
  it alone), so Android's volume alone controls loudness. Microphone capture
  is opened in the vendor HAL as `VOICE_RECOGNITION`, libasp's ASR path as
  Fire OS uses it for Alexa: the seven calibrated microphones
  (`/proc/idme/miccal.*`) go through Amazon's AEC and beamformer, without NR
  or AGC (`persist.vendor.biscuit.mic_source`; 7 selects the
  `VOICE_COMMUNICATION` call path with AEC, NR and AGC but no beamformer).
  Android adds the AOSP/WebRTC AGC on top for `mic` and `voice_recognition`
  (`configs/audio/audio_effects.xml`), as CM14 did, so one AGC runs.
- **Bluetooth speaker**: A2DP sink and AVRCP controller only; BiscuitService
  answers pairing requests (PIN `0000` or just-works) and connects A2DP sink
  after bonding.
- **Microphone privacy**: as on Fire OS, the kernel owns it. `amz_priv`
  mutes GPIO87 (global GPIO 444) when the microphone button is
  released and unmutes it on the next press, even if Android is hung.
  Software can enter privacy (`privacy_trigger`) but never leave it: the HAL
  wrapper refuses an unmute while `privacy_state` is 1, so
  `biscuit_service mic unmute` only works after the button. `biscuit-buttond`
  follows `privacy_state` into Android's microphone mute flag. Both live
  under `/dev/biscuit_privacy` (the keypad's `amz_privacy` sysfs group).
- **Action button**: `biscuit-buttond` also reads `KEY_HELP` from the keypad
  and sends every press and release, also while asleep, as
  `com.amazon.biscuit.service.ACTION_BUTTON` (`down`, `eventTime` in
  uptime milliseconds) through `am broadcast`. `ro.biscuit.action_broadcast=1`
  advertises it, so Kiosk Satellite needs no accessibility service for the
  button. The key is not grabbed; Android still sees it.
- **Volume buttons**: Android handles them as usual until an app claims them
  through BiscuitService's `claimVolumeButtons()`, the Kiosk Satellite
  Edition interface (`ro.biscuit.volume_claim=1` advertises it). While the
  app's binder token lives, `sys.biscuit.volume_claimed` is 1, the
  `frameworks/base` patch sends each press to BiscuitService, and
  BiscuitService broadcasts it to that app as
  `com.amazon.biscuit.service.VOLUME_BUTTON` (`direction` `up` or `down`)
  without changing the volume. Kiosk Satellite uses it to steer a Music
  Assistant group. `biscuit_service volume up|down` always changes the
  Dot's own volume.
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
  to finish booting). Its `overrides` drop the launchers, Settings,
  LineageOS's settings screens (`LineageParts`), `SimpleDeviceConfig`, the
  setup wizard, updater, user apps, keyboard, wallpapers, themes and printing
  from the image, and the services a speaker without a secure element or a screen does not use:
  SecureElement, the ClearKey and CAS HALs (Widevine stays), Perfetto's
  `traced`, and the storage, VPN, proxy, captive portal, certificate and
  companion device apps.
- **Disabled services**: `securetime`, as on rook: the TEE rejects its calls
  every 5 seconds, and Widevine L3 does not need it. `cameraserver`: Biscuit
  has no camera, so the image also leaves out mt8163-common's camera HAL
  (`camerahalserver` and its 57 files, 14 MB), the libraries built for it and
  the camera feature files.
- **Features**: `biscuit_unavailable_features.xml` takes back what a headless
  speaker lacks (touchscreen, motion sensors, location, Ethernet, USB host,
  WebView, SIP, NFC), so system_server and the lineage-sdk skip the printing,
  MIDI, backup, app widget, companion device, autofill, Ethernet, LiveDisplay,
  profile, power menu and trust services.

## Status

Verified on the host:

- Host tests pass for the bootargs allowlist, amonet slot parsing, the OTA
  write-target checks, the headless HWC (Android 11 headers),
  `biscuit-buttond` (privacy state and Action button events) and
  `sensors.biscuit` (both light sensor sources), with ASan/UBSan.
- The audio wrapper compiles for arm32 against Android 11 headers with its
  struct-layout asserts; the shim exports exactly the symbols the blobs
  import; BiscuitService compiles against the Android 11 API signatures it
  uses.
- `extract-files.sh --only-target` extracts all 48 blobs and applies every
  fixup.

Verified on the device (2026-10-20, slot B, `4.9.337`): Android 11 boots;
Wi-Fi and Bluetooth come up, and a phone streams to the Dot as an A2DP sink;
playback reaches the speaker at 48 kHz; the microphone array captures (PCM 22,
9 channels at 16 kHz) and a voice satellite's wake word triggers; the light
sensor reads plausible values (about 2500 lx at a sunny window); the LED ring
plays its animations and shows the volume; zram swaps with lz4 (about 2.7:1)
and KSM merges pages; with all four cores loaded at 1.3 GHz for 2.5 minutes
the CPU settles at about 57 °C, well below the first trip point at 84 °C;
Widevine runs at L3 (system ID 4445), provisions and gets a license from
Google's test server. The kernel's console survives a reboot in pstore
(`/sys/fs/pstore/console-ramoops`).

Open work is tracked in [TODO.md](TODO.md); the history and decision record
is the [port study](docs/lineage-18.1-port-study.md).

### How 4.9 got to boot

The first 4.9 images seemed not to get past LK. The kernel did start (kaeru
switches LK to 64-bit; the kernel enters at EL2 with a valid DTB at
`0x48000000`); pstore breadcrumbs then showed where it stopped:

| Problem | Fix (`android_kernel_amazon_biscuit`, `lineage-18.1`) |
| --- | --- |
| Hang right after `paging_init` | Reserve the top 8 MiB below the 504 MiB Fire OS 6's LK reports (`0x5f800000`, ATF/TEE secure memory) |
| USB oops on a VBUS `ERR_PTR` | Build `usb20` with `OTG_WITHOUT_VBUS_CHECK` for Biscuit |
| SMI larb 2 NULL dereference | Keep `imgsys` enabled; it is larb 2's clock controller |
| Sound card did not register | Use the TI DAC links (as on rook) |
| Audio routed to the headset | Read the jack GPIO as on rook |
| Playback stalled, every write timed out | Keep mt8163.dtsi's audio clocks (Biscuit builds without `CONFIG_SND_I2S_MCLK`, so rook's list left every clock unprepared), and start the AFE with DL1 |

In this tree: the mt66xx-common patch that requests the connsys ROM patch by
its base name (Wi-Fi and Bluetooth) and the `frameworks/av` TimeCheck patch.

## Support

If this port is useful to you, you can support the work on
[Ko-fi](https://ko-fi.com/meiser79).
