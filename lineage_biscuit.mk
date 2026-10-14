#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Headless: there is no screen to accept the ADB RSA prompt.
WITH_ADB_INSECURE := true

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

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
