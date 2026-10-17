#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Headless: there is no screen to accept the ADB RSA prompt.
WITH_ADB_INSECURE := true

# Inherit from those products. Most specific first.
# This is full_base.mk without languages_full.mk: its ~80 locales end up in
# every APK's resources.arsc (44 MB in Settings alone), and system is only
# 768 MiB. PRODUCT_LOCALES is a merged list, so it can only shrink by not
# inheriting them.
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_no_telephony.mk)
$(call inherit-product-if-exists, frameworks/base/data/sounds/AudioPackage14.mk)
$(call inherit-product-if-exists, vendor/google/security/adb/vendor_key.mk)

PRODUCT_PACKAGES += \
    audio.a2dp.default \
    libfwdlockengine \
    netutils-wrapper-1.0 \
    preinstalled-packages-platform-full-base.xml

PRODUCT_LOCALES := en_US de_DE

# Inherit from device makefile.
$(call inherit-product, device/amazon/biscuit/device.mk)

# Inherit some common LineageOS stuff.
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)

PRODUCT_NAME := lineage_biscuit
PRODUCT_DEVICE := biscuit
PRODUCT_MANUFACTURER := Amzn
PRODUCT_BRAND := google
PRODUCT_MODEL := Echo Dot
PRODUCT_CHARACTERISTICS := tablet,nosdcard

WITHOUT_SETUP_WIZARD := true
