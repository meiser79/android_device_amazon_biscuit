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

# Camera: Biscuit has none. mt8163-common copies the Echo Show camera HAL,
# its libraries and the camera feature files; nothing else on /vendor needs
# them. PRODUCT_COPY_FILES only becomes read-only after the board config, so
# drop them here.
BISCUIT_CAMERA_BLOBS := \
    android.hardware.camera.front.xml \
    android.hardware.camera.xml \
    camerahalserver \
    camerahalserver.rc \
    android.hardware.camera.provider@2.4-impl-mediatek.so \
    vendor.mediatek.hardware.camera.ccap@1.0-impl.so \
    lib3am.so \
    libcam.camadapter.so \
    libcam.campipe.so \
    libcam.camshot.so \
    libcam.client.so \
    libcam.device1.so \
    libcam.exif.so \
    libcam.exif.v3.so \
    libcam.hal3a.v3.dng.so \
    libcam.hal3a.v3.so \
    libcam.halsensor.so \
    libcam.iopipe.so \
    libcam.iopipe_FrmB.so \
    libcam.metadataprovider.so \
    libcam.paramsmgr.so \
    libcam.utils.sensorlistener.so \
    libcam.utils.so \
    libcam1_utils.so \
    libcam3_hwnode.so \
    libcam3_hwpipeline.so \
    libcam3_pipeline.so \
    libcam3_utils.so \
    libcam_hwutils.so \
    libcam_mmp.so \
    libcam_platform.so \
    libcam_utils.so \
    libcamalgo.so \
    libcamdrv.so \
    libcamdrv_FrmB.so \
    libccap.so \
    libccci_util.so \
    libfeatureio.so \
    libfeatureiodrv.so \
    libhdrproc.so \
    libimageio.so \
    libimageio_FrmB.so \
    libimageio_plat_drv.so \
    libimageio_plat_drv_FrmB.so \
    libispfeaturem.so \
    libmtkcam_debugutils.so \
    libmtkcam_device1.so \
    libmtkcam_device3.so \
    libmtkcam_device3_app.so \
    libmtkcam_fwkutils.so \
    libmtkcam_grallocutils.so \
    libmtkcam_imgbuf.so \
    libmtkcam_metadata.so \
    libmtkcam_stdutils.so \
    libmtkcam_sysutils.so \
    libmtkcam_ulog.so \
    vendor.mediatek.hardware.camera.ccap@1.0.so \
    vendor.mediatek.hardware.camera.device@1.1.so \
    vendor.mediatek.hardware.power@2.0.so
PRODUCT_COPY_FILES := $(foreach cf,$(PRODUCT_COPY_FILES),$(if \
    $(filter $(BISCUIT_CAMERA_BLOBS),$(notdir $(word 2,$(subst :, ,$(cf))))),,$(cf)))

# HIDL (after mt8163-common's manifest, so that its overrides apply)
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/manifest_biscuit.xml

# Boot image and kernel
# The arm64 4.9 kernel as Image.gz-dtb, mt8163-common's format. Stock LK
# forces 32-bit kernels; amonet v2's kaeru switches it to 64-bit when
# mt8163-common's bootopt asks for it (bootopt=64S3,32N2,64N2). Kernel
# 0x40080000, second 0x40f00000 and tags 0x48000000 are mt8163-common's
# addresses; its ramdisk (0x69244e00) lies past Biscuit's 512 MB, so the
# ramdisk goes to the stock 0x44000000, below the ram_console and pstore
# carve-outs at 0x44400000. The kernel forces its own command line
# (biscuit_defconfig); LK only reads bootopt from the boot image.
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_OFFSET := 0x00080000
BOARD_RAMDISK_OFFSET := 0x04000000
BOARD_SECOND_OFFSET := 0x00f00000
BOARD_KERNEL_TAGS_OFFSET := 0x08000000
BOARD_MKBOOTIMG_ARGS := \
    --kernel_offset $(BOARD_KERNEL_OFFSET) \
    --header_version $(BOARD_BOOTIMG_HEADER_VERSION) \
    --ramdisk_offset $(BOARD_RAMDISK_OFFSET) \
    --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) \
    --second_offset $(BOARD_SECOND_OFFSET)

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
