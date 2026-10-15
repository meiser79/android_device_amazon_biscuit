#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, frameworks/native/build/phone-hdpi-512-dalvik-heap.mk)

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := mdpi

# Audio
# audio.primary.biscuit runs the Fire OS 6 HAL (audio.primary_amazon.mt8163)
# under the Android 11 ABI; see audio/audio_hw.c.
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.biscuit \
    libbinder_shim \
    libbiscuit_audio_shim \
    libtinyalsa_shim

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(LOCAL_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml

# Biscuit service: LED ring, microphone button and the biscuit_service CLI
PRODUCT_PACKAGES += \
    BiscuitService \
    biscuit-privacyd \
    biscuit-ledctl \
    biscuit-ledd \
    biscuit_service \
    i2c-poke

PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*.animation,$(LOCAL_PATH)/biscuit-service/animations,$(TARGET_COPY_OUT_SYSTEM)/etc/biscuit-ledd)

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-service \
    android.hardware.bluetooth@1.0-impl \
    libbt-vendor

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml

# Display
# Biscuit has no panel: hwcomposer.biscuit is a headless HWC 1.1 that the
# composer 2.1 passthrough service wraps with HWC2On1Adapter.
PRODUCT_PACKAGES += \
    hwcomposer.biscuit

# Ambient light sensor (TSL2584TSV over IIO or TSL2540 over input)
PRODUCT_PACKAGES += \
    sensors.biscuit

# Audio debugging, as on CM14 (tinymix and tinycap come from the common tree)
PRODUCT_PACKAGES_DEBUG += \
    tinypcminfo \
    tinyplay

# Headless HOME; its overrides drop the launchers and user-facing apps.
PRODUCT_PACKAGES += \
    BiscuitEmptyLauncher

# Fstab (slotted amonet v2 layout; overrides the mt8163-common legacy fstab)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.mt8163 \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_RAMDISK)/fstab.mt8163

# Init scripts
PRODUCT_PACKAGES += \
    init.biscuit.connsys.sh \
    init.target.biscuit.rc

# Keylayout (microphone and action buttons)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/keylayout/mtk-kpd.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/mtk-kpd.kl

# Kernel modules
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/init.insmod.cfg:$(TARGET_COPY_OUT_VENDOR)/etc/init.insmod.cfg

PRODUCT_PACKAGES += \
    mt66xx_bt \
    mt66xx_common \
    mt66xx_wlan

# Overlays (Bluetooth: A2DP sink speaker, no source/PAN)
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay

PRODUCT_ENFORCE_RRO_EXCLUDED_OVERLAYS += \
    $(LOCAL_PATH)/overlay/packages/apps/Bluetooth

# Screen
TARGET_SCREEN_DENSITY := 213
TARGET_SCREEN_HEIGHT := 320
TARGET_SCREEN_WIDTH := 320

# Shipping API Level (Fire OS 6, Android 7.1)
PRODUCT_SHIPPING_API_LEVEL := 25

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# Vendor partition
TARGET_HAS_VENDOR_PARTITION := false

# Wi-Fi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service-lazy \
    libwifi-hal

PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(LOCAL_PATH)/configs/wifi/,$(TARGET_COPY_OUT_VENDOR)/etc/wifi)

# STA only: replaces the wifi.direct feature that mt8163-common declares
# (the first PRODUCT_COPY_FILES entry for a destination wins).
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml

# Inherit from mt8163-common
$(call inherit-product, device/amazon/mt8163-common/mt8163.mk)

# Inherit the proprietary files
$(call inherit-product, vendor/amazon/biscuit/biscuit-vendor.mk)
