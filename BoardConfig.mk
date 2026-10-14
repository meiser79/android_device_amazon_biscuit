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

# Kernel
# biscuit_defconfig forces its own command line (the Fire OS LK passes an
# unsafe one), so BOARD_KERNEL_CMDLINE from mt8163-common only reaches LK,
# which reads bootopt= from it to boot the arm64 kernel.
TARGET_KERNEL_SOURCE := kernel/amazon/biscuit

# Recovery
# amonet v2 provides TWRP; this fstab only describes the slotted partitions
# to the OTA tools. releasetools/ redirects OTA writes to TWRP's live
# /dev/block/current-{system,boot} aliases.
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/fstab.mt8163

# Releasetools (replaces the mt8163-common extension)
TARGET_RELEASETOOLS_EXTENSIONS := $(DEVICE_PATH)/releasetools

# Inherit the proprietary files
include vendor/amazon/biscuit/BoardConfigVendor.mk
