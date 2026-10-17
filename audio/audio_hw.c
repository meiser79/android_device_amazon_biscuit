/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * audio.primary.biscuit: runs Biscuit's Fire OS 6 (Android 7.1) audio HAL,
 * audio.primary_amazon.mt8163.so, under the Android 11 HAL ABI.
 *
 * - Translates the structs whose layout changed since Android 7.1 (device
 *   vtable, audio_config, audio_port_config).
 * - Wraps every stream so the framework never reads past the end of the
 *   smaller Android 7.1 stream vtables, and every vendor callback gets the
 *   vendor's own stream pointer.
 * - Switches the TLV320DAC3203 speaker route and amplifier around playback,
 *   as the stock headless product does outside the HAL. The amplifier only
 *   comes on after the first write, once the vendor HAL has started I2S and
 *   powered up the DAC and its output driver, and goes off before the
 *   stream stops; switching it with the codec avoids the crackle of a cold
 *   codec powering up into a live amplifier.
 */

#define LOG_TAG "audio.primary.biscuit"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <time.h>

#include <cutils/properties.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#include "audio_legacy.h"

#define CARD 0

/*
 * How long playback runs before the speaker amplifier comes on. 0: right
 * after the first write, which started I2S and the DAC. A longer delay
 * swallows short sounds (chimes) that start from standby.
 */
#define AMP_DELAY_PROP "persist.vendor.biscuit.amp_delay_ms"
#define AMP_DELAY_DEFAULT_MS 0

/*
 * libasp's playback path applies Amazon's device volume (music_volTab in
 * AFE.cfg, index 0-30) on top of Android's. Fire OS's volume service sets it
 * over LIPC, which goes nowhere here (lipc/), so set it to the 0 dB top and
 * leave the volume to Android. Command 5 / index 30 are amazon-oss's
 * ASP_CMD_SET_VOLUME and ASP_VOLUME_MAX; -1 in either property disables it.
 */
#define ASP_VOLUME_PROP "persist.vendor.biscuit.asp_volume"
#define ASP_VOLUME_CMD_PROP "persist.vendor.biscuit.asp_volume_cmd"
#define ASP_CMD_SET_VOLUME 5
#define ASP_VOLUME_MAX 30

/* amz_priv sysfs group of the keypad device (init.target.biscuit.rc) */
#define PRIVACY_STATE "/dev/biscuit_privacy/privacy_state"
#define PRIVACY_TRIGGER "/dev/biscuit_privacy/privacy_trigger"

struct biscuit_audio_device {
    struct audio_hw_device device;
    struct legacy_audio_hw_device* legacy;
    bool mic_muted;
};

struct biscuit_stream_out {
    struct audio_stream_out stream;
    struct audio_stream_out* legacy;
    bool active;
    bool amp_on;
    int64_t start_ns;
};

struct biscuit_stream_in {
    struct audio_stream_in stream;
    struct audio_stream_in* legacy;
};

static struct legacy_audio_hw_device* legacy_dev(const struct audio_hw_device* dev) {
    return ((const struct biscuit_audio_device*)dev)->legacy;
}

static struct audio_stream_out* legacy_out(const struct audio_stream* stream) {
    return ((const struct biscuit_stream_out*)stream)->legacy;
}

static struct audio_stream_in* legacy_in(const struct audio_stream* stream) {
    return ((const struct biscuit_stream_in*)stream)->legacy;
}

/* Mixer routing (verified on CM14 with tinyplay) */

static int route_enum_or_value(struct mixer* mixer, const char* name, const char* value,
                               int fallback) {
    struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, name);
    unsigned int i, n;
    int ret;

    if (!ctl) {
        ALOGW("missing mixer ctl: %s", name);
        return -ENOENT;
    }
    ret = mixer_ctl_set_enum_by_string(ctl, value);
    if (ret == 0) return 0;

    n = mixer_ctl_get_num_values(ctl);
    for (i = 0; i < n; i++) {
        ret = mixer_ctl_set_value(ctl, i, fallback);
        if (ret != 0) ALOGW("failed mixer ctl %s[%u]=%d", name, i, fallback);
    }
    return ret;
}

static int route_value(struct mixer* mixer, const char* name, int left, int right) {
    struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, name);
    unsigned int n;
    int ret = 0;

    if (!ctl) {
        ALOGW("missing mixer ctl: %s", name);
        return -ENOENT;
    }
    n = mixer_ctl_get_num_values(ctl);
    if (n > 0) ret = mixer_ctl_set_value(ctl, 0, left);
    if (n > 1) ret |= mixer_ctl_set_value(ctl, 1, right);
    if (ret != 0) ALOGW("failed mixer ctl %s=%d,%d", name, left, right);
    return ret;
}

static void apply_speaker_route(void) {
    struct mixer* mixer = mixer_open(CARD);

    if (!mixer) {
        ALOGE("mixer_open(%d) failed", CARD);
        return;
    }
    route_enum_or_value(mixer, "HPL Output Mixer L_DAC Switch", "On", 1);
    route_enum_or_value(mixer, "HPR Output Mixer R_DAC Switch", "On", 1);
    route_enum_or_value(mixer, "Audio_DacMux_Setting", "Off", 0);
    route_enum_or_value(mixer, "Right Channel Only", "On", 1);
    route_value(mixer, "HP Driver Gain Volume", 6, 6);
    route_enum_or_value(mixer, "MFP Gpio Mute", "Off", 0);
    mixer_close(mixer);
}

static void set_speaker_amp(bool enabled) {
    struct mixer* mixer = mixer_open(CARD);

    if (!mixer) {
        ALOGE("mixer_open(%d) failed", CARD);
        return;
    }
    if (enabled) {
        route_enum_or_value(mixer, "Audio_Amp_R_Switch", "On", 1);
        route_enum_or_value(mixer, "Audio_Amp_L_Switch", "On", 1);
        route_enum_or_value(mixer, "Ext_Speaker_Amp_Switch", "On", 1);
    } else {
        route_enum_or_value(mixer, "Ext_Speaker_Amp_Switch", "Off", 0);
        route_enum_or_value(mixer, "Audio_Amp_R_Switch", "Off", 0);
        route_enum_or_value(mixer, "Audio_Amp_L_Switch", "Off", 0);
    }
    mixer_close(mixer);
}

typedef int (*asp_command_t)(void* pipeline, int code, int size, void* data, void* reply,
                             int* reply_size);

static void set_asp_volume(void) {
    static asp_command_t asp_command;
    static bool looked_up;
    int volume = property_get_int32(ASP_VOLUME_PROP, ASP_VOLUME_MAX);
    int cmd = property_get_int32(ASP_VOLUME_CMD_PROP, ASP_CMD_SET_VOLUME);
    int ret;

    if (volume < 0 || cmd < 0) return;
    if (!looked_up) {
        /* Loaded by the vendor HAL; only look it up, never load it here. */
        void* handle = dlopen("libasp.so", RTLD_NOW | RTLD_NOLOAD);

        asp_command = handle ? (asp_command_t)dlsym(handle, "asp_command") : NULL;
        if (!asp_command) ALOGW("libasp asp_command not available");
        looked_up = true;
    }
    if (!asp_command) return;
    ret = asp_command(NULL, cmd, sizeof(volume), &volume, NULL, NULL);
    ALOGI("ASP command %d (volume %d) returned %d", cmd, volume, ret);
}

/* Struct translation */

static void to_legacy_config(const struct audio_config* config,
                             struct legacy_audio_config* legacy) {
    const audio_offload_info_t* info = &config->offload_info;
    struct legacy_audio_offload_info* legacy_info = &legacy->offload_info;

    memset(legacy, 0, sizeof(*legacy));
    legacy->sample_rate = config->sample_rate;
    legacy->channel_mask = config->channel_mask;
    legacy->format = config->format;
    legacy->frame_count = config->frame_count;

    legacy_info->version = AUDIO_MAKE_OFFLOAD_INFO_VERSION(0, 1);
    legacy_info->size = sizeof(*legacy_info);
    legacy_info->sample_rate = info->sample_rate;
    legacy_info->channel_mask = info->channel_mask;
    legacy_info->format = info->format;
    legacy_info->stream_type = info->stream_type;
    legacy_info->bit_rate = info->bit_rate;
    legacy_info->duration_us = info->duration_us;
    legacy_info->has_video = info->has_video;
    legacy_info->is_streaming = info->is_streaming;
    legacy_info->bit_width = info->bit_width;
    legacy_info->offload_buffer_size = info->offload_buffer_size;
    legacy_info->usage = info->usage;
}

static void from_legacy_config(const struct legacy_audio_config* legacy,
                               struct audio_config* config) {
    config->sample_rate = legacy->sample_rate;
    config->channel_mask = legacy->channel_mask;
    config->format = legacy->format;
    config->frame_count = legacy->frame_count;
}

static void to_legacy_port_config(const struct audio_port_config* config,
                                  struct legacy_audio_port_config* legacy) {
    memcpy(legacy, config, offsetof(struct legacy_audio_port_config, ext));
    memcpy(&legacy->ext, &config->ext, sizeof(legacy->ext));
}

/* Output streams */

static uint32_t out_get_sample_rate(const struct audio_stream* stream) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_sample_rate(&l->common);
}

static int out_set_sample_rate(struct audio_stream* stream, uint32_t rate) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.set_sample_rate(&l->common, rate);
}

static size_t out_get_buffer_size(const struct audio_stream* stream) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_buffer_size(&l->common);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream* stream) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_channels(&l->common);
}

static audio_format_t out_get_format(const struct audio_stream* stream) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_format(&l->common);
}

static int out_set_format(struct audio_stream* stream, audio_format_t format) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.set_format(&l->common, format);
}

static int out_standby(struct audio_stream* stream) {
    struct biscuit_stream_out* out = (struct biscuit_stream_out*)stream;
    struct audio_stream_out* l = out->legacy;
    int ret = 0;

    /* Amplifier off first, so it does not amplify the DAC powering down. */
    if (out->amp_on) {
        set_speaker_amp(false);
        out->amp_on = false;
    }
    if (l->common.standby) ret = l->common.standby(&l->common);
    out->active = false;
    return ret;
}

static int out_dump(const struct audio_stream* stream, int fd) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.dump(&l->common, fd);
}

static audio_devices_t out_get_device(const struct audio_stream* stream) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_device(&l->common);
}

static int out_set_device(struct audio_stream* stream, audio_devices_t device) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.set_device(&l->common, device);
}

static int out_set_parameters(struct audio_stream* stream, const char* kv_pairs) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.set_parameters(&l->common, kv_pairs);
}

static char* out_get_parameters(const struct audio_stream* stream, const char* keys) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.get_parameters(&l->common, keys);
}

static int out_add_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.add_audio_effect(&l->common, effect);
}

static int out_remove_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream_out* l = legacy_out(stream);
    return l->common.remove_audio_effect(&l->common, effect);
}

static uint32_t out_get_latency(const struct audio_stream_out* stream) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->get_latency(l);
}

static int out_set_volume(struct audio_stream_out* stream, float left, float right) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->set_volume(l, left, right);
}

static int64_t now_ns(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static ssize_t out_write(struct audio_stream_out* stream, const void* buffer, size_t bytes) {
    struct biscuit_stream_out* out = (struct biscuit_stream_out*)stream;
    ssize_t ret;

    if (!out->active) {
        apply_speaker_route();
        out->active = true;
        out->start_ns = now_ns();
        set_asp_volume();
    }
    /* The first write opens the PCM: it starts I2S and powers up the DAC. */
    ret = out->legacy->write(out->legacy, buffer, bytes);
    if (!out->amp_on) {
        int delay_ms = property_get_int32(AMP_DELAY_PROP, AMP_DELAY_DEFAULT_MS);

        if (now_ns() - out->start_ns >= (int64_t)delay_ms * 1000000LL) {
            set_speaker_amp(true);
            out->amp_on = true;
        }
    }
    return ret;
}

static int out_get_render_position(const struct audio_stream_out* stream, uint32_t* dsp_frames) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->get_render_position(l, dsp_frames);
}

static int out_get_next_write_timestamp(const struct audio_stream_out* stream,
                                        int64_t* timestamp) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->get_next_write_timestamp(l, timestamp);
}

static int out_set_callback(struct audio_stream_out* stream, stream_callback_t callback,
                            void* cookie) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->set_callback(l, callback, cookie);
}

static int out_pause(struct audio_stream_out* stream) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->pause(l);
}

static int out_resume(struct audio_stream_out* stream) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->resume(l);
}

static int out_drain(struct audio_stream_out* stream, audio_drain_type_t type) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->drain(l, type);
}

static int out_flush(struct audio_stream_out* stream) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->flush(l);
}

static int out_get_presentation_position(const struct audio_stream_out* stream,
                                         uint64_t* frames, struct timespec* timestamp) {
    struct audio_stream_out* l = legacy_out(&stream->common);
    return l->get_presentation_position(l, frames, timestamp);
}

/* Only the Android 7.1 members are set; newer ones stay NULL. */
static void wrap_stream_out(struct biscuit_stream_out* out) {
    const struct audio_stream_out* l = out->legacy;
    struct audio_stream_out* s = &out->stream;

    if (l->common.get_sample_rate) s->common.get_sample_rate = out_get_sample_rate;
    if (l->common.set_sample_rate) s->common.set_sample_rate = out_set_sample_rate;
    if (l->common.get_buffer_size) s->common.get_buffer_size = out_get_buffer_size;
    if (l->common.get_channels) s->common.get_channels = out_get_channels;
    if (l->common.get_format) s->common.get_format = out_get_format;
    if (l->common.set_format) s->common.set_format = out_set_format;
    s->common.standby = out_standby;
    if (l->common.dump) s->common.dump = out_dump;
    if (l->common.get_device) s->common.get_device = out_get_device;
    if (l->common.set_device) s->common.set_device = out_set_device;
    if (l->common.set_parameters) s->common.set_parameters = out_set_parameters;
    if (l->common.get_parameters) s->common.get_parameters = out_get_parameters;
    if (l->common.add_audio_effect) s->common.add_audio_effect = out_add_audio_effect;
    if (l->common.remove_audio_effect) s->common.remove_audio_effect = out_remove_audio_effect;
    if (l->get_latency) s->get_latency = out_get_latency;
    if (l->set_volume) s->set_volume = out_set_volume;
    if (l->write) s->write = out_write;
    if (l->get_render_position) s->get_render_position = out_get_render_position;
    if (l->get_next_write_timestamp) s->get_next_write_timestamp = out_get_next_write_timestamp;
    if (l->set_callback) s->set_callback = out_set_callback;
    if (l->pause) s->pause = out_pause;
    if (l->resume) s->resume = out_resume;
    if (l->drain) s->drain = out_drain;
    if (l->flush) s->flush = out_flush;
    if (l->get_presentation_position)
        s->get_presentation_position = out_get_presentation_position;
}

/* Input streams */

static uint32_t in_get_sample_rate(const struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_sample_rate(&l->common);
}

static int in_set_sample_rate(struct audio_stream* stream, uint32_t rate) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.set_sample_rate(&l->common, rate);
}

static size_t in_get_buffer_size(const struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_buffer_size(&l->common);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_channels(&l->common);
}

static audio_format_t in_get_format(const struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_format(&l->common);
}

static int in_set_format(struct audio_stream* stream, audio_format_t format) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.set_format(&l->common, format);
}

static int in_standby(struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.standby(&l->common);
}

static int in_dump(const struct audio_stream* stream, int fd) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.dump(&l->common, fd);
}

static audio_devices_t in_get_device(const struct audio_stream* stream) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_device(&l->common);
}

static int in_set_device(struct audio_stream* stream, audio_devices_t device) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.set_device(&l->common, device);
}

static int in_set_parameters(struct audio_stream* stream, const char* kv_pairs) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.set_parameters(&l->common, kv_pairs);
}

static char* in_get_parameters(const struct audio_stream* stream, const char* keys) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.get_parameters(&l->common, keys);
}

static int in_add_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.add_audio_effect(&l->common, effect);
}

static int in_remove_audio_effect(const struct audio_stream* stream, effect_handle_t effect) {
    struct audio_stream_in* l = legacy_in(stream);
    return l->common.remove_audio_effect(&l->common, effect);
}

static int in_set_gain(struct audio_stream_in* stream, float gain) {
    struct audio_stream_in* l = legacy_in(&stream->common);
    return l->set_gain(l, gain);
}

static ssize_t in_read(struct audio_stream_in* stream, void* buffer, size_t bytes) {
    struct audio_stream_in* l = legacy_in(&stream->common);
    return l->read(l, buffer, bytes);
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in* stream) {
    struct audio_stream_in* l = legacy_in(&stream->common);
    return l->get_input_frames_lost(l);
}

static int in_get_capture_position(const struct audio_stream_in* stream, int64_t* frames,
                                   int64_t* time) {
    struct audio_stream_in* l = legacy_in(&stream->common);
    return l->get_capture_position(l, frames, time);
}

static void wrap_stream_in(struct biscuit_stream_in* in) {
    const struct audio_stream_in* l = in->legacy;
    struct audio_stream_in* s = &in->stream;

    if (l->common.get_sample_rate) s->common.get_sample_rate = in_get_sample_rate;
    if (l->common.set_sample_rate) s->common.set_sample_rate = in_set_sample_rate;
    if (l->common.get_buffer_size) s->common.get_buffer_size = in_get_buffer_size;
    if (l->common.get_channels) s->common.get_channels = in_get_channels;
    if (l->common.get_format) s->common.get_format = in_get_format;
    if (l->common.set_format) s->common.set_format = in_set_format;
    if (l->common.standby) s->common.standby = in_standby;
    if (l->common.dump) s->common.dump = in_dump;
    if (l->common.get_device) s->common.get_device = in_get_device;
    if (l->common.set_device) s->common.set_device = in_set_device;
    if (l->common.set_parameters) s->common.set_parameters = in_set_parameters;
    if (l->common.get_parameters) s->common.get_parameters = in_get_parameters;
    if (l->common.add_audio_effect) s->common.add_audio_effect = in_add_audio_effect;
    if (l->common.remove_audio_effect) s->common.remove_audio_effect = in_remove_audio_effect;
    if (l->set_gain) s->set_gain = in_set_gain;
    if (l->read) s->read = in_read;
    if (l->get_input_frames_lost) s->get_input_frames_lost = in_get_input_frames_lost;
    if (l->get_capture_position) s->get_capture_position = in_get_capture_position;
}

/* Device */

static int adev_init_check(const struct audio_hw_device* dev) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->init_check(l);
}

static int adev_set_voice_volume(struct audio_hw_device* dev, float volume) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->set_voice_volume(l, volume);
}

static int adev_set_master_volume(struct audio_hw_device* dev, float volume) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->set_master_volume(l, volume);
}

static int adev_get_master_volume(struct audio_hw_device* dev, float* volume) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->get_master_volume(l, volume);
}

static int adev_set_mode(struct audio_hw_device* dev, audio_mode_t mode) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->set_mode(l, mode);
}

/* Returns 1 while amz_priv keeps the microphones private, 0, or -1. */
static int kernel_privacy(void) {
    char c = 0;
    int fd = open(PRIVACY_STATE, O_RDONLY | O_CLOEXEC);

    if (fd < 0) return -1;
    if (read(fd, &c, 1) != 1) c = 0;
    close(fd);
    return c == '1' ? 1 : (c == '0' ? 0 : -1);
}

static void enter_kernel_privacy(void) {
    int fd = open(PRIVACY_TRIGGER, O_WRONLY | O_CLOEXEC);

    if (fd < 0 || write(fd, "1", 1) != 1) ALOGW("cannot enter kernel privacy: %s", strerror(errno));
    if (fd >= 0) close(fd);
}

/*
 * Microphone privacy belongs to the kernel (amz_priv on the microphone
 * button), as on Fire OS: software may enter it but never leave it, so an
 * unmute is refused while the kernel is private.
 *
 * The vendor HAL drives GPIO87 as well and then may fail to notify Amazon's
 * ASP service, which does not run here. Keep the mute state ourselves so
 * AudioFlinger sees it applied.
 */
static int adev_set_mic_mute(struct audio_hw_device* dev, bool state) {
    struct biscuit_audio_device* adev = (struct biscuit_audio_device*)dev;
    int ret;

    if (!state && kernel_privacy() == 1) {
        ALOGI("microphone privacy is on; only the microphone button unmutes");
        adev->mic_muted = true;
        return 0;
    }
    if (state) enter_kernel_privacy();
    ret = adev->legacy->set_mic_mute(adev->legacy, state);
    if (ret != 0) ALOGW("vendor set_mic_mute(%d) returned %d", state, ret);
    adev->mic_muted = state;
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device* dev, bool* state) {
    const struct biscuit_audio_device* adev = (const struct biscuit_audio_device*)dev;

    if (!state) return -EINVAL;
    *state = adev->mic_muted || kernel_privacy() == 1;
    return 0;
}

static int adev_set_parameters(struct audio_hw_device* dev, const char* kv_pairs) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->set_parameters(l, kv_pairs);
}

static char* adev_get_parameters(const struct audio_hw_device* dev, const char* keys) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->get_parameters(l, keys);
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device* dev,
                                         const struct audio_config* config) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct legacy_audio_config legacy_config;

    to_legacy_config(config, &legacy_config);
    return l->get_input_buffer_size(l, &legacy_config);
}

static int adev_open_output_stream(struct audio_hw_device* dev, audio_io_handle_t handle,
                                   audio_devices_t devices, audio_output_flags_t flags,
                                   struct audio_config* config,
                                   struct audio_stream_out** stream_out, const char* address) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct legacy_audio_config legacy_config;
    struct audio_stream_out* legacy = NULL;
    struct biscuit_stream_out* out;
    int ret;

    to_legacy_config(config, &legacy_config);
    ret = l->open_output_stream(l, handle, devices, flags, &legacy_config, &legacy, address);
    from_legacy_config(&legacy_config, config);
    if (ret != 0) return ret;
    if (!legacy) return -ENODEV;

    out = calloc(1, sizeof(*out));
    if (!out) {
        l->close_output_stream(l, legacy);
        return -ENOMEM;
    }
    out->legacy = legacy;
    wrap_stream_out(out);
    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device* dev,
                                     struct audio_stream_out* stream) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct biscuit_stream_out* out = (struct biscuit_stream_out*)stream;

    out_standby(&out->stream.common);
    l->close_output_stream(l, out->legacy);
    free(out);
}

static int adev_open_input_stream(struct audio_hw_device* dev, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config* config,
                                  struct audio_stream_in** stream_in, audio_input_flags_t flags,
                                  const char* address, audio_source_t source) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct legacy_audio_config legacy_config;
    struct audio_stream_in* legacy = NULL;
    struct biscuit_stream_in* in;
    int ret;

    to_legacy_config(config, &legacy_config);
    ret = l->open_input_stream(l, handle, devices, &legacy_config, &legacy, flags, address,
                               source);
    from_legacy_config(&legacy_config, config);
    if (ret != 0) return ret;
    if (!legacy) return -ENODEV;

    in = calloc(1, sizeof(*in));
    if (!in) {
        l->close_input_stream(l, legacy);
        return -ENOMEM;
    }
    in->legacy = legacy;
    wrap_stream_in(in);
    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device* dev, struct audio_stream_in* stream) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct biscuit_stream_in* in = (struct biscuit_stream_in*)stream;

    l->close_input_stream(l, in->legacy);
    free(in);
}

static int adev_dump(const struct audio_hw_device* dev, int fd) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->dump(l, fd);
}

static int adev_set_master_mute(struct audio_hw_device* dev, bool mute) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->set_master_mute(l, mute);
}

static int adev_get_master_mute(struct audio_hw_device* dev, bool* mute) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->get_master_mute(l, mute);
}

static int adev_create_audio_patch(struct audio_hw_device* dev, unsigned int num_sources,
                                   const struct audio_port_config* sources,
                                   unsigned int num_sinks,
                                   const struct audio_port_config* sinks,
                                   audio_patch_handle_t* handle) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    struct legacy_audio_port_config legacy_sources[AUDIO_PATCH_PORTS_MAX];
    struct legacy_audio_port_config legacy_sinks[AUDIO_PATCH_PORTS_MAX];
    unsigned int i;

    if (num_sources > AUDIO_PATCH_PORTS_MAX || num_sinks > AUDIO_PATCH_PORTS_MAX)
        return -EINVAL;
    for (i = 0; i < num_sources; i++) to_legacy_port_config(&sources[i], &legacy_sources[i]);
    for (i = 0; i < num_sinks; i++) to_legacy_port_config(&sinks[i], &legacy_sinks[i]);
    return l->create_audio_patch(l, num_sources, legacy_sources, num_sinks, legacy_sinks,
                                 handle);
}

static int adev_release_audio_patch(struct audio_hw_device* dev, audio_patch_handle_t handle) {
    struct legacy_audio_hw_device* l = legacy_dev(dev);
    return l->release_audio_patch(l, handle);
}

static int adev_close(struct hw_device_t* device) {
    struct biscuit_audio_device* adev = (struct biscuit_audio_device*)device;
    int ret = 0;

    set_speaker_amp(false);
    if (adev->legacy && adev->legacy->common.close)
        ret = adev->legacy->common.close(&adev->legacy->common);
    free(adev);
    return ret;
}

/* get_audio_port/set_audio_port_config stay NULL: their structs changed and
 * the framework falls back when they are missing. */
static void wrap_device(struct biscuit_audio_device* adev) {
    const struct legacy_audio_hw_device* l = adev->legacy;
    struct audio_hw_device* d = &adev->device;

    if (l->init_check) d->init_check = adev_init_check;
    if (l->set_voice_volume) d->set_voice_volume = adev_set_voice_volume;
    if (l->set_master_volume) d->set_master_volume = adev_set_master_volume;
    if (l->get_master_volume) d->get_master_volume = adev_get_master_volume;
    if (l->set_mode) d->set_mode = adev_set_mode;
    if (l->set_mic_mute) d->set_mic_mute = adev_set_mic_mute;
    d->get_mic_mute = adev_get_mic_mute;
    if (l->set_parameters) d->set_parameters = adev_set_parameters;
    if (l->get_parameters) d->get_parameters = adev_get_parameters;
    if (l->get_input_buffer_size) d->get_input_buffer_size = adev_get_input_buffer_size;
    if (l->open_output_stream) d->open_output_stream = adev_open_output_stream;
    if (l->close_output_stream) d->close_output_stream = adev_close_output_stream;
    if (l->open_input_stream) d->open_input_stream = adev_open_input_stream;
    if (l->close_input_stream) d->close_input_stream = adev_close_input_stream;
    if (l->dump) d->dump = adev_dump;
    if (l->set_master_mute) d->set_master_mute = adev_set_master_mute;
    if (l->get_master_mute) d->get_master_mute = adev_get_master_mute;
    if (l->create_audio_patch) d->create_audio_patch = adev_create_audio_patch;
    if (l->release_audio_patch) d->release_audio_patch = adev_release_audio_patch;
}

static int adev_open(const struct hw_module_t* module, const char* name,
                     struct hw_device_t** device) {
    struct biscuit_audio_device* adev;
    const struct hw_module_t* vendor_module;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;

    adev = calloc(1, sizeof(*adev));
    if (!adev) return -ENOMEM;

    ret = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, "primary_amazon", &vendor_module);
    if (ret != 0) {
        ALOGE("cannot load Fire OS primary audio module: %d", ret);
        goto fail;
    }
    if (!vendor_module->methods || !vendor_module->methods->open) {
        ALOGE("Fire OS primary audio module has no open method");
        ret = -ENOSYS;
        goto fail;
    }
    ret = vendor_module->methods->open(vendor_module, name,
                                       (struct hw_device_t**)&adev->legacy);
    if (ret != 0) {
        ALOGE("cannot open Fire OS primary audio device: %d", ret);
        goto fail;
    }

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = adev->legacy->common.version;
    adev->device.common.module = (struct hw_module_t*)module;
    adev->device.common.close = adev_close;
    wrap_device(adev);
    set_speaker_amp(false);
    set_asp_volume();

    *device = &adev->device.common;
    return 0;

fail:
    free(adev);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Biscuit Fire OS 6 audio HAL wrapper",
        .author = "The LineageOS Project",
        .methods = &hal_module_methods,
    },
};
