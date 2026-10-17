#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

"""Generate recovery OTAs for amonet v2 TWRP's active slot on Biscuit.

amonet v2 keeps the native A/B GPT (boot_a/b, system_a/b) and its TWRP
exposes the active slot as /dev/block/current-system and
/dev/block/current-boot. Edify does not apply slotselect, so full OTAs are
pointed at those aliases. Nothing else may be written: the recovery
partition holds TWRP.
"""

import re

import common

_CURRENT_SLOT_DEVICES = {
    "/system": "/dev/block/current-system",
    "/boot": "/dev/block/current-boot",
}

# Fail closed unless TWRP's aliases match the slot amonet v2 will boot.
_AMONET_SLOT_ASSERTION = (
    'assert(run_program("/sbin/sh", "-c", '
    '"test -x /sbin/amzn_bcbtool || exit 1; '
    'slot=$(/sbin/amzn_bcbtool get_active); '
    'system=$(readlink -f /dev/block/current-system); '
    'boot=$(readlink -f /dev/block/current-boot); '
    'case $slot in '
    'a) test $system = /dev/block/mmcblk0p13 && test $boot = /dev/block/mmcblk0p10 ;; '
    'b) test $system = /dev/block/mmcblk0p14 && test $boot = /dev/block/mmcblk0p11 ;; '
    '*) exit 1 ;; '
    'esac") == "0" || abort("amonet v2 slot check failed: '
    'current-system/current-boot do not match the active slot"));'
)

_WRITE_COMMANDS = ("block_image_update", "package_extract_file", "write_raw_image")

# addon.d v1 scripts (Magisk) may finish post-restore in a background job;
# wait for it (at most 120 s) so the install only completes, and the user
# only reboots, once the boot image is final. 'post-[r]estore' matches the
# job's command line but not its own text in grep's or this shell's.
_WAIT_FOR_ADDOND = (
    'ui_print("Waiting for addon.d background jobs...");\n'
    'run_program("/sbin/sh", "-c", "i=0; while [ $i -lt 120 ]; do '
    "grep -qs 'post-[r]estore' /proc/[0-9]*/cmdline || exit 0; "
    'sleep 1; i=$((i + 1)); done; exit 0");'
)
_DEVICE_RE = re.compile(r'"(/dev/block/[^"]*)"')


def _use_current_slot_devices(info):
    """Point /system and /boot at TWRP's slot aliases before any diff is built."""
    fstab = info.info_dict["fstab"]
    for mount_point, device in _CURRENT_SLOT_DEVICES.items():
        if mount_point not in fstab:
            raise ValueError("missing %s in OTA fstab" % mount_point)
        fstab[mount_point].device = device
        fstab[mount_point].slotselect = False


def _replace_device_assertion(info):
    for index, line in enumerate(info.script.script):
        if line.startswith('assert(getprop("ro.product.device")'):
            info.script.script[index] = _AMONET_SLOT_ASSERTION
            return
    raise ValueError("missing default OTA device assertion")


def _check_write_targets(info):
    """Refuse any updater write outside the two current-slot aliases."""
    allowed = set(_CURRENT_SLOT_DEVICES.values())
    for line in info.script.script:
        if "recovery" in line and "write_raw_image" in line:
            raise ValueError("OTA must not write the recovery partition")
        if not any(command in line for command in _WRITE_COMMANDS):
            continue
        for device in _DEVICE_RE.findall(line):
            if device not in allowed:
                raise ValueError("OTA writes %s; only %s are allowed" %
                                 (device, ", ".join(sorted(allowed))))


def FullOTA_Assertions(info):
    if common.OPTIONS.two_step:
        raise ValueError("two-step OTAs would rewrite recovery (amonet v2 TWRP)")
    # Runs before GetBlockDifferences(), so system and boot pick these up.
    _use_current_slot_devices(info)
    _replace_device_assertion(info)


def _write_boot_before_addond_restore(info):
    """Write boot.img before backuptool restores the addon.d scripts.

    The generic updater restores addon.d and then writes boot.img. Magisk's
    addon.d (v1, for non-A/B updates) patches the boot image from a
    background job that only waits five seconds, so it raced the boot write,
    and a reboot right after the install could leave a half written boot
    image. With boot.img written first, addon.d works on the final image,
    and the updater then waits for that job before it finishes.
    """
    script = info.script.script
    restores = [i for i, line in enumerate(script)
                if 'backuptool.sh", "restore"' in line]
    if not restores:
        return  # backuptool disabled: nothing to reorder
    boots = [i for i, line in enumerate(script)
             if line.startswith('package_extract_file("boot.img", ')]
    if len(restores) != 1 or len(boots) != 1 or boots[0] < restores[0]:
        raise ValueError("unexpected updater layout: addon.d restore at %s, "
                         "boot.img write at %s" % (restores, boots))
    boot = script.pop(boots[0])
    script.insert(restores[0], boot)
    script.insert(restores[0] + 2, _WAIT_FOR_ADDOND)


def FullOTA_InstallEnd(info):
    _write_boot_before_addond_restore(info)
    _check_write_targets(info)


def IncrementalOTA_Assertions(info):
    # Block diffs resolve their devices before this hook runs, so the
    # current-slot aliases cannot be enforced for incremental packages.
    raise ValueError("incremental OTAs are not supported on amonet v2; "
                     "build a full OTA")
