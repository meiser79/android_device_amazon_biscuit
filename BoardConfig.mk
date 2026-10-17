#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/amazon/biscuit

# Bluetooth (MT8163 connsys, open-source libbt-vendor from hardware/mediatek/mt66xx)
BOARD_HAVE_BLUETOOTH := true
BOARD_MTK_BT_CHIP := MTK_CONSYS_MT8163
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := biscuit

# HIDL
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/manifest_biscuit.xml

# Kernel
TARGET_KERNEL_CONFIG := biscuit_defconfig

# Partitions (native amonet v2 GPT: boot_a/b, system_a/b)
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_CACHEIMAGE_PARTITION_SIZE := 822083584
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 805306368
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1325383168

# Properties
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/system.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Wi-Fi (MT8163 connsys through /dev/wmtWifi)
BOARD_WLAN_DEVICE := MediaTek
BOARD_MEDIATEK_COMBO_CHIP := mt66xx
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_mt66xx
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_mt66xx
WIFI_DRIVER_FW_PATH_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_FW_PATH_STA := "STA"
WIFI_DRIVER_FW_PATH_AP := "AP"
WIFI_DRIVER_FW_PATH_P2P := "P2P"
WIFI_DRIVER_STATE_CTRL_PARAM := "/dev/wmtWifi"
WIFI_DRIVER_STATE_ON := "1"
WIFI_DRIVER_STATE_OFF := "0"

# Inherit from mt8163-common
include device/amazon/mt8163-common/BoardConfigCommon.mk

# Boot image and kernel
# The amonet v2 boot chain (Fire OS 6 preloader, LK and ATF) only starts
# 32-bit kernels: a 64-bit kernel, even stock Fire OS 5's, never gets past
# LK. Boot the Fire OS 6 3.18 kernel as an ARM zImage-dtb with the Fire OS 6
# and CM14 layout: kernel 0x40008000, ramdisk 0x44000000 (below the
# ram_console carve-out at 0x44400000), second 0x40f00000, tags 0x48000000.
# LK reads bootopt= from the boot image to pick the 32-bit kernel mode; the
# kernel forces its own command line (biscuit_defconfig) and drops the rest.
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_SECOND_OFFSET := 0x00f00000
BOARD_KERNEL_TAGS_OFFSET := 0x08000000
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,32N2 androidboot.selinux=permissive
BOARD_MKBOOTIMG_ARGS := \
    --kernel_offset $(BOARD_KERNEL_OFFSET) \
    --header_version $(BOARD_BOOTIMG_HEADER_VERSION) \
    --ramdisk_offset $(BOARD_RAMDISK_OFFSET) \
    --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) \
    --second_offset $(BOARD_SECOND_OFFSET)

TARGET_KERNEL_ARCH := arm
TARGET_KERNEL_SOURCE := kernel/amazon/biscuit
BOARD_KERNEL_IMAGE_NAME := zImage-dtb
# mt8163-common points at an aarch64 Linaro GCC; 3.18 builds with the
# tree's own ARM GCC 4.9.
TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(shell pwd)/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androidkernel-

# Recovery
# amonet v2 provides TWRP; this fstab only describes the slotted partitions
# to the OTA tools. releasetools/ redirects OTA writes to TWRP's live
# /dev/block/current-{system,boot} aliases.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/fstab.mt8163

# Releasetools (replaces the mt8163-common extension)
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)/releasetools

# Inherit the proprietary files
include vendor/amazon/biscuit/BoardConfigVendor.mk
