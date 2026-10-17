/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * sensors.biscuit: Biscuit's ambient light sensor (sensors HAL API 1.3).
 *
 * Boards carry either an AMS TSL2584TSV, read through IIO
 * (illuminance0_input), or a TAOS TSL2540 on i2c0, read through its sysfs
 * group (als_lux) and powered with als_power_state while the sensor is active.
 * The sensors service has no input group, so /dev/input is not an option.
 */

#define LOG_TAG "sensors.biscuit"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/sensors.h>
#include <log/log.h>

#ifndef IIO_DIR
#define IIO_DIR "/sys/bus/iio/devices"
#endif
#ifndef TSL2540_DIR
#define TSL2540_DIR "/sys/bus/i2c/devices/0-0039"
#endif

#define LIGHT_HANDLE 1
#define MIN_DELAY_US 200000
#define MAX_DELAY_US 1000000
#define IDLE_POLL_NS 500000000LL

struct biscuit_poll_device {
    struct sensors_poll_device_1 device;  // must stay the first member
    pthread_mutex_t lock;
    int active;
    int flush_pending;
    int64_t delay_ns;
    int has_last;
    float last_lux;
    char lux_path[PATH_MAX];  // illuminance0_input (IIO) or als_lux (TSL2540)
    int tsl2540;              // lux_path is the TSL2540's als_lux
};

static const struct sensor_t sSensorList[] = {{
        .name = "Biscuit ambient light",
        .vendor = "AMS/TAOS",
        .version = 1,
        .handle = LIGHT_HANDLE,
        .type = SENSOR_TYPE_LIGHT,
        .maxRange = 65535.0f,
        .resolution = 1.0f,
        .power = 0.1f,
        .minDelay = 0,  // on-change sensors report 0
        .fifoReservedEventCount = 0,
        .fifoMaxEventCount = 0,
        .stringType = SENSOR_STRING_TYPE_LIGHT,
        .requiredPermission = "",
        .maxDelay = MAX_DELAY_US,
        .flags = SENSOR_FLAG_ON_CHANGE_MODE,
        .reserved = {0},
}};

static int find_iio_path(char* out, size_t out_len) {
    DIR* dir = opendir(IIO_DIR);
    struct dirent* de;

    if (!dir) return -errno;
    while ((de = readdir(dir)) != NULL) {
        char path[PATH_MAX];

        if (strncmp(de->d_name, "iio:device", 10) != 0) continue;
        snprintf(path, sizeof(path), IIO_DIR "/%s/illuminance0_input", de->d_name);
        if (access(path, R_OK) == 0) {
            strlcpy(out, path, out_len);
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -ENOENT;
}

static int find_tsl2540_path(char* out, size_t out_len) {
    if (access(TSL2540_DIR "/als_lux", R_OK) != 0) return -ENOENT;
    strlcpy(out, TSL2540_DIR "/als_lux", out_len);
    return 0;
}

static void set_tsl2540_power(int on) {
    int fd = open(TSL2540_DIR "/als_power_state", O_WRONLY | O_CLOEXEC);

    if (fd < 0 || write(fd, on ? "1" : "0", 1) != 1) {
        ALOGW("cannot power the TSL2540 %s: %s", on ? "on" : "off", strerror(errno));
    }
    if (fd >= 0) close(fd);
}

static int read_lux(struct biscuit_poll_device* dev, float* lux) {
    FILE* f;
    float value = 0.0f;
    int ok;

    if (!dev->lux_path[0]) return -ENOENT;
    f = fopen(dev->lux_path, "r");
    if (!f) return -errno;
    ok = fscanf(f, "%f", &value);
    fclose(f);
    if (ok != 1) return -EIO;
    *lux = value < 0.0f ? 0.0f : value;
    return 0;
}

static int64_t now_ns(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_ns(int64_t ns) {
    struct timespec ts;

    if (ns < 10000000LL) ns = 10000000LL;
    ts.tv_sec = ns / 1000000000LL;
    ts.tv_nsec = ns % 1000000000LL;
    nanosleep(&ts, NULL);
}

static int poll__activate(struct sensors_poll_device_t* d, int handle, int enabled) {
    struct biscuit_poll_device* dev = (struct biscuit_poll_device*)d;

    if (handle != LIGHT_HANDLE) return -EINVAL;
    pthread_mutex_lock(&dev->lock);
    enabled = enabled ? 1 : 0;
    if (enabled && !dev->active) {
        dev->has_last = 0;
        if (!dev->lux_path[0] && find_iio_path(dev->lux_path, sizeof(dev->lux_path)) < 0 &&
            find_tsl2540_path(dev->lux_path, sizeof(dev->lux_path)) == 0) {
            dev->tsl2540 = 1;
        }
        if (!dev->lux_path[0]) ALOGW("no ambient light sensor found");
        if (dev->tsl2540) set_tsl2540_power(1);
    } else if (!enabled && dev->active && dev->tsl2540) {
        set_tsl2540_power(0);
    }
    dev->active = enabled;
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__setDelay(struct sensors_poll_device_t* d, int handle, int64_t ns) {
    struct biscuit_poll_device* dev = (struct biscuit_poll_device*)d;

    if (handle != LIGHT_HANDLE) return -EINVAL;
    if (ns < (int64_t)MIN_DELAY_US * 1000LL) ns = (int64_t)MIN_DELAY_US * 1000LL;
    if (ns > (int64_t)MAX_DELAY_US * 1000LL) ns = (int64_t)MAX_DELAY_US * 1000LL;
    pthread_mutex_lock(&dev->lock);
    dev->delay_ns = ns;
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__batch(struct sensors_poll_device_1* d, int handle, int flags,
                       int64_t period_ns, int64_t timeout) {
    (void)flags;
    (void)timeout;  // no FIFO: batching degrades to continuous reporting
    return poll__setDelay((struct sensors_poll_device_t*)d, handle, period_ns);
}

static int poll__flush(struct sensors_poll_device_1* d, int handle) {
    struct biscuit_poll_device* dev = (struct biscuit_poll_device*)d;

    if (handle != LIGHT_HANDLE) return -EINVAL;
    pthread_mutex_lock(&dev->lock);
    dev->flush_pending++;
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int poll__poll(struct sensors_poll_device_t* d, sensors_event_t* data, int count) {
    struct biscuit_poll_device* dev = (struct biscuit_poll_device*)d;
    int active;
    int64_t delay;
    float lux = 0.0f;
    int n = 0;

    if (count < 1) return -EINVAL;

    pthread_mutex_lock(&dev->lock);
    if (dev->flush_pending > 0) {
        dev->flush_pending--;
        pthread_mutex_unlock(&dev->lock);
        memset(data, 0, sizeof(*data));
        data->version = META_DATA_VERSION;
        data->type = SENSOR_TYPE_META_DATA;
        data->meta_data.what = META_DATA_FLUSH_COMPLETE;
        data->meta_data.sensor = LIGHT_HANDLE;
        return 1;
    }
    active = dev->active;
    delay = dev->delay_ns;
    pthread_mutex_unlock(&dev->lock);

    sleep_ns(active ? delay : IDLE_POLL_NS);
    if (!active) return 0;

    pthread_mutex_lock(&dev->lock);
    if (dev->active && read_lux(dev, &lux) == 0 && (!dev->has_last || lux != dev->last_lux)) {
        dev->has_last = 1;
        dev->last_lux = lux;
        memset(data, 0, sizeof(*data));
        data->version = sizeof(*data);
        data->sensor = LIGHT_HANDLE;
        data->type = SENSOR_TYPE_LIGHT;
        data->timestamp = now_ns();
        data->light = lux;
        n = 1;
    }
    pthread_mutex_unlock(&dev->lock);
    return n;
}

static int poll__close(struct hw_device_t* d) {
    struct biscuit_poll_device* dev = (struct biscuit_poll_device*)d;

    if (dev->active && dev->tsl2540) set_tsl2540_power(0);
    pthread_mutex_destroy(&dev->lock);
    free(dev);
    return 0;
}

static int sensors__get_sensors_list(struct sensors_module_t* module,
                                     struct sensor_t const** list) {
    (void)module;
    *list = sSensorList;
    return sizeof(sSensorList) / sizeof(sSensorList[0]);
}

static int open_sensors(const struct hw_module_t* module, const char* name,
                        struct hw_device_t** device) {
    struct biscuit_poll_device* dev;

    if (strcmp(name, SENSORS_HARDWARE_POLL) != 0) return -EINVAL;
    dev = calloc(1, sizeof(*dev));
    if (!dev) return -ENOMEM;

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    dev->device.common.module = (struct hw_module_t*)module;
    dev->device.common.close = poll__close;
    dev->device.activate = poll__activate;
    dev->device.setDelay = poll__setDelay;
    dev->device.poll = poll__poll;
    dev->device.batch = poll__batch;
    dev->device.flush = poll__flush;
    dev->delay_ns = (int64_t)MIN_DELAY_US * 1000LL;
    pthread_mutex_init(&dev->lock, NULL);

    *device = &dev->device.common;
    return 0;
}

static struct hw_module_methods_t sensors_module_methods = {
        .open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
        .common =
                {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = SENSORS_HARDWARE_MODULE_ID,
                        .name = "Biscuit sensors",
                        .author = "Biscuit",
                        .methods = &sensors_module_methods,
                },
        .get_sensors_list = sensors__get_sensors_list,
};
