#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, frameworks/native/build/phone-xhdpi-512-dalvik-heap.mk)

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := mdpi

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

# Fstab (slotted amonet v2 layout; overrides the mt8163-common legacy fstab)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.mt8163 \
    $(LOCAL_PATH)/configs/fstab.mt8163:$(TARGET_COPY_OUT_RAMDISK)/fstab.mt8163

# Init scripts
PRODUCT_PACKAGES += \
    init.target.biscuit.rc

# Kernel modules
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/init.insmod.cfg:$(TARGET_COPY_OUT_VENDOR)/etc/init.insmod.cfg

PRODUCT_PACKAGES += \
    mt66xx_bt \
    mt66xx_common \
    mt66xx_wlan

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

# Inherit from mt8163-common
$(call inherit-product, device/amazon/mt8163-common/mt8163.mk)

# Inherit the proprietary files
$(call inherit-product, vendor/amazon/biscuit/biscuit-vendor.mk)
