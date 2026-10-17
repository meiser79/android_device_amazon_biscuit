/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Biscuit has no battery and its kernel registers no power supply, so the
 * default health HAL reports no AC, no battery and an unknown status. The
 * framework then treats the speaker as unplugged: SystemUI keeps asking for
 * a time-remaining estimate, and jobs that require charging never run.
 *
 * Android 7's healthd reported such devices as always plugged into AC mains
 * (BatteryMonitor's mAlwaysPluggedDevice); CM14 relied on it. Android 11
 * dropped that, so report the same values here.
 */

#include <memory>
#include <string_view>

#include <health/utils.h>
#include <health2impl/Health.h>

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V1_0::BatteryHealth;
using ::android::hardware::health::V1_0::BatteryStatus;
using ::android::hardware::health::V2_1::HealthInfo;
using ::android::hardware::health::V2_1::IHealth;
using namespace std::literals;

namespace {

// Android 7 healthd's ALWAYS_PLUGGED_CAPACITY and FAKE_BATTERY_TEMPERATURE
constexpr int32_t kAlwaysPluggedCapacity = 100;
constexpr int32_t kFakeBatteryTemperature = 424;

class BiscuitHealth : public ::android::hardware::health::V2_1::implementation::Health {
  public:
    using Health::Health;

    Return<void> getCapacity(getCapacity_cb _hidl_cb) override {
        getHealthInfo_2_1([&](auto res, const auto& info) {
            _hidl_cb(res, info.legacy.legacy.batteryLevel);
        });
        return Void();
    }

    Return<void> getChargeStatus(getChargeStatus_cb _hidl_cb) override {
        getHealthInfo_2_1([&](auto res, const auto& info) {
            _hidl_cb(res, info.legacy.legacy.batteryStatus);
        });
        return Void();
    }

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        auto& info = health_info->legacy.legacy;

        if (info.batteryPresent || info.chargerAcOnline || info.chargerUsbOnline ||
            info.chargerWirelessOnline) {
            return;
        }
        info.chargerAcOnline = true;
        info.batteryPresent = true;
        info.batteryStatus = BatteryStatus::CHARGING;
        info.batteryHealth = BatteryHealth::GOOD;
        info.batteryLevel = kAlwaysPluggedCapacity;
        info.batteryTemperature = kFakeBatteryTemperature;
    }
};

}  // namespace

extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
    if (instance != "default"sv) {
        return nullptr;
    }
    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());
    return new BiscuitHealth(std::move(config));
}
