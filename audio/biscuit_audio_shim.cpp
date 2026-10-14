/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Android 7.1 symbols that Biscuit's Fire OS 6 audio closure imports and
 * Android 11 no longer provides. Added to the blobs with patchelf.
 */

#include <stdint.h>
#include <time.h>

#include <tinyalsa/asoundlib.h>

extern "C" {

/* libacehal_log.so: liblog dropped this no-op flush. */
void android_logger_flush(void) {}

/* audio.primary_amazon.mt8163.so: legacy TinyALSA helper. */
int pcm_get_trigger_tstamp(struct pcm* pcm, struct timespec* tstamp) {
    unsigned int avail;

    return pcm_get_htimestamp(pcm, &avail, tstamp);
}

}  // extern "C"

namespace android {

/* libutils dropped LinearTransform; the HAL and libcommon_time_client use reduce(). */
struct LinearTransform {
    template <class T>
    static void reduce(T* N, T* D);
};

template <class T>
void LinearTransform::reduce(T* N, T* D) {
    T a, b;

    if (!N || !D || !(*D)) return;

    a = *N;
    b = *D;
    if (a == 0) {
        *D = 1;
        return;
    }

    // Euclid's algorithm for the greatest common divisor.
    if (a < b) {
        T tmp = a;
        a = b;
        b = tmp;
    }
    while (true) {
        T tmp = a % b;
        if (!tmp) break;
        a = b;
        b = tmp;
    }

    *N /= b;
    *D /= b;
}

template void LinearTransform::reduce<uint64_t>(uint64_t* N, uint64_t* D);
template void LinearTransform::reduce<uint32_t>(uint32_t* N, uint32_t* D);

}  // namespace android
