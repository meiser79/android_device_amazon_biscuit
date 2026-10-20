# TODO

Open work on the 4.9 port (`lineage-18.1`). What already works is in the
README's [Status](README.md#status).

## Microphone: long-run check

The mic signal passes three stages:

1. Amazon's voice-assistant processing (libasp's "ASR path"): removes what
   the Echo itself plays and focuses on the talker's direction across all
   mics. It does not filter steady noise.
2. Android's level control (WebRTC AGC in fixed-digital mode, from
   `configs/audio/audio_effects.xml`): works like a compressor, adding up
   to 9 dB and capping peaks just below full scale (-3 dBFS).
3. Kiosk Satellite, with its own echo removal and noise filter turned off,
   and a fixed mic gain. Keep that gain at 0 dB unless the level check
   below says otherwise.

Home Assistant does no noise filtering of its own, and Kiosk Satellite
ignores Home Assistant's noise setting. It sounds fine so far; what's left
is to use it for a while and watch for these:

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Wake word fires on its own, or commands are misheard, with a fan, fridge or TV in the background | Nothing filters steady noise, and the level control turns it up in quiet pauses | Turn on Kiosk Satellite's Noise Suppression; if that's not enough, add Android's `ns` effect to `mic` in `audio_effects.xml` |
| Loud speech sounds distorted (clipped) | The level control already caps peaks just below full scale, so any positive Kiosk Satellite mic gain after it pushes them over | Set Kiosk Satellite's mic gain to 0 dB or below; if it still clips, remove the AGC from `audio_effects.xml` (and `patches/device/amazon/mt8163-common/0002-*` for the other Echos) and set the level with Kiosk Satellite's mic gain alone |
| Wake word only works close up | Input level too low | Check the level in Kiosk Satellite's Wake Word Tester at normal distance (aim for about 0.05); only if it's clearly lower, raise its mic gain a few dB. The CM14 image's higher mic preamp (36 instead of 20 dB, `audio_device.xml`) was only needed because Amazon's processing never ran there |
| Wake word or commands are missed while music plays loud | Amazon's echo removal leaves some music behind | Test with music at 100%; if it fails, port the CM14 image's beamformer that blocks the speaker's direction (`mic_beam.c`) |
| Kiosk Satellite's own chimes or replies trigger the wake word | Its echo removal is off | Turn Kiosk Satellite's echo cancellation back on |

As a last resort, `persist.vendor.biscuit.mic_source=7`
(`persist.vendor.amazon.mic_source` on the other Echos) switches to
Amazon's phone-call processing: it filters noise and controls the level,
but does not focus on the talker.

## Memory

- Optional: `kmalloc-64` holds about 177,000 objects (11 MiB), flat
  between 7 and 41 minutes of uptime, so not a leak. About 6 MiB of it is
  KSM's `ksm_rmap_item` (98,584 rmap items), which pays for itself (KSM
  shares 10,097 pages, 39 MiB). The other ~5 MiB come from the caches SLUB
  merges into it (`iommu_iova`, `ashmem_range_cache`, `gralloc_extra`,
  `tcp_bind_bucket`, ...); a boot with `slub_nomerge` would separate them,
  for at most 2-3 MiB. The rest of the 54 MiB of unreclaimable slab is
  ordinary: process mappings (`vm_area_struct`, `anon_vma*`, 16 MiB) and
  threads.

## Audio restarts

- The audio HAL takes more than 5 s to open after an audioserver restart
  (Amazon's `libasp` initialises in `adev_open`), so every restart logs two
  TimeCheck timeouts with a HAL tombstone each. The TimeCheck patch keeps
  audioserver working; starting `libasp` off the open path would avoid them.

## Suspend

- Resume oopsed in `read_md32_cfgreg()`: SPM's sleep code reads md32's
  registers, and Biscuit's device tree (like stock's) left md32 disabled.
  The kernel now enables it, as on the other MT8163 Echos; verify on the
  device that md32 loads its firmware and that the Dot resumes from an RTC
  wake alarm (`echo +30 > /sys/class/rtc/rtc0/wakealarm`, then
  `echo mem > /sys/power/state`).
  Suspend itself also warns about an unbalanced USB clock unprepare in
  `musb_suspend_noirq()`. `stay_on_while_plugged_in` defaults to AC power
  anyway, since a suspended Dot hears nothing; installs from before need
  `adb shell settings put global stay_on_while_plugged_in 1`.

## Risks

- The mic array's FPGA reset and programming delays (`FPGA_DELAY_MS`,
  `PINCTRL_DELAY_MS`) are 2 ms on 4.9, inherited from cronos, against 100 ms on
  3.18. If the microphones ever fail to start after a boot, raise them.
- netd's bandwidth accounting is inactive: its iptables batch uses
  `-m owner --socket-exists` in `raw` PREROUTING, which needs `xt_qtaguid`,
  and the 4.9 tree has none, so the `bw_*` chains stay empty (no per-app
  traffic statistics or data limits; networking itself works). Never set
  `ro.kernel.ebpf.supported` to switch netd to BPF instead: starting
  bpfloader with it froze the Dot, with nothing in pstore.

## Upstream

- kaeru's A/B slot fix is merged upstream
  ([`ab93750f`](https://github.com/R0rt1z2/kaeru/commit/ab93750f2475a8ca7e51a82168634f48d62e3cfa)).
  Once an amonet release ships it, drop the kaeru build from
  `tools/bootchain/` and the prebuilt zip from the [README](README.md).

## Port from the Kiosk Satellite Edition (CM14.1)

From [`jxlarrea/lineageos-biscuit-kiosk-satellite`](https://github.com/jxlarrea/lineageos-biscuit-kiosk-satellite):

- **Amazon's wake word engine** (`biscuit-pryond`, its `c0702183` and
  `21ff2234`). Kiosk Satellite's `pryon` wake word engine sends audio to
  `/dev/socket/biscuit-pryon`, and the service runs Fire OS's Pryon engine
  on it: "Alexa" from the 6.5.7.4 OTA, plus Echo, Computer, Amazon, Ziggy
  and 16 more locales from `amazon-davs-models`. The engine is
  `libpryon.so` and 15 more 32-bit libraries from the OTA's `/system/lib`,
  built for Android 7. First check that they load under Android 11's
  linker (namespaces, their own `libc++_shared.so`).
- Optional: **self-update** (its `c572f428` and `f1303e1d`). BiscuitService
  checks GitHub releases, verifies the download's checksum and signature,
  and queues the install in TWRP. `tools/biscuit-ota.sh` already does this
  from a PC.

Not needed: its speaker high-pass and limiter (patch 035) ship off since
its `3a85c153` and `52fed08b`, as they hurt Kiosk Satellite's echo
cancellation and the wake word. Its own mic array processing
(`persist.biscuit.mic_beam_out` and the speaker removal) replaces Amazon's,
which this tree runs instead.
