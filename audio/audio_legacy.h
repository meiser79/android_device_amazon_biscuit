/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hardware/audio.h>
#include <system/audio.h>

/*
 * Layouts of the Android 7.1 audio HAL ABI that Biscuit's Fire OS 6
 * audio.primary_amazon.mt8163.so was built against. Only the members that
 * changed position or size since then are redeclared here; stream vtables
 * only grew at the end and keep using the current structs.
 */

/* audio_offload_info_t before version 0.2 added encapsulation and tuner ids. */
struct legacy_audio_offload_info {
    uint16_t version;
    uint16_t size;
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_stream_type_t stream_type;
    uint32_t bit_rate;
    int64_t duration_us;
    bool has_video;
    bool is_streaming;
    uint32_t bit_width;
    uint32_t offload_buffer_size;
    audio_usage_t usage;
};

struct legacy_audio_config {
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct legacy_audio_offload_info offload_info;
    size_t frame_count;
};

/* audio_port_config before the flags field was added in front of ext. */
struct legacy_audio_port_config {
    audio_port_handle_t id;
    audio_port_role_t role;
    audio_port_type_t type;
    unsigned int config_mask;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct audio_gain_config gain;
    union {
        struct audio_port_config_device_ext device;
        struct audio_port_config_mix_ext mix;
        struct audio_port_config_session_ext session;
    } ext;
};

/* audio_hw_device before get_microphones was inserted after dump. */
struct legacy_audio_hw_device {
    struct hw_device_t common;
    uint32_t (*get_supported_devices)(const struct legacy_audio_hw_device* dev);
    int (*init_check)(const struct legacy_audio_hw_device* dev);
    int (*set_voice_volume)(struct legacy_audio_hw_device* dev, float volume);
    int (*set_master_volume)(struct legacy_audio_hw_device* dev, float volume);
    int (*get_master_volume)(struct legacy_audio_hw_device* dev, float* volume);
    int (*set_mode)(struct legacy_audio_hw_device* dev, audio_mode_t mode);
    int (*set_mic_mute)(struct legacy_audio_hw_device* dev, bool state);
    int (*get_mic_mute)(const struct legacy_audio_hw_device* dev, bool* state);
    int (*set_parameters)(struct legacy_audio_hw_device* dev, const char* kv_pairs);
    char* (*get_parameters)(const struct legacy_audio_hw_device* dev, const char* keys);
    size_t (*get_input_buffer_size)(const struct legacy_audio_hw_device* dev,
                                    const struct legacy_audio_config* config);
    int (*open_output_stream)(struct legacy_audio_hw_device* dev, audio_io_handle_t handle,
                              audio_devices_t devices, audio_output_flags_t flags,
                              struct legacy_audio_config* config,
                              struct audio_stream_out** stream_out, const char* address);
    void (*close_output_stream)(struct legacy_audio_hw_device* dev,
                                struct audio_stream_out* stream_out);
    int (*open_input_stream)(struct legacy_audio_hw_device* dev, audio_io_handle_t handle,
                             audio_devices_t devices, struct legacy_audio_config* config,
                             struct audio_stream_in** stream_in, audio_input_flags_t flags,
                             const char* address, audio_source_t source);
    void (*close_input_stream)(struct legacy_audio_hw_device* dev,
                               struct audio_stream_in* stream_in);
    int (*dump)(const struct legacy_audio_hw_device* dev, int fd);
    int (*set_master_mute)(struct legacy_audio_hw_device* dev, bool mute);
    int (*get_master_mute)(struct legacy_audio_hw_device* dev, bool* mute);
    int (*create_audio_patch)(struct legacy_audio_hw_device* dev, unsigned int num_sources,
                              const struct legacy_audio_port_config* sources,
                              unsigned int num_sinks,
                              const struct legacy_audio_port_config* sinks,
                              audio_patch_handle_t* handle);
    int (*release_audio_patch)(struct legacy_audio_hw_device* dev, audio_patch_handle_t handle);
    /* get_audio_port and set_audio_port_config follow; their structs changed too. */
};

_Static_assert(offsetof(struct legacy_audio_offload_info, usage) ==
                       offsetof(audio_offload_info_t, usage),
               "legacy offload info must match the current one up to usage");
_Static_assert(offsetof(struct legacy_audio_port_config, ext) ==
                       offsetof(struct audio_port_config, flags),
               "legacy port config must match the current one up to ext");
_Static_assert(offsetof(struct legacy_audio_hw_device, close_input_stream) ==
                       offsetof(struct audio_hw_device, close_input_stream),
               "legacy device must match the current one up to close_input_stream");
_Static_assert(offsetof(struct legacy_audio_hw_device, dump) ==
                       offsetof(struct audio_hw_device, get_microphones),
               "Android 7.1 has dump where get_microphones is now");
#if defined(__arm__)
_Static_assert(sizeof(struct legacy_audio_offload_info) == 48, "Android 7.1 arm32 layout");
_Static_assert(offsetof(struct legacy_audio_config, frame_count) == 64, "Android 7.1 arm32 layout");
#endif
