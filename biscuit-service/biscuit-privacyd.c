/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * biscuit-privacyd: the kernel (amz_priv) mutes the microphones itself when
 * the microphone button is pressed. Follow its privacy_state and tell
 * BiscuitService, so Android's microphone mute state matches the hardware.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PRIVACY_STATE
#define PRIVACY_STATE "/dev/biscuit_privacy/privacy_state"
#endif
#ifndef AM_BIN
#define AM_BIN "/system/bin/am"
#endif
/* Re-read now and then in case a sysfs_notify() is missed. */
#ifndef POLL_MS
#define POLL_MS 5000
#endif

/* Returns 1 (private), 0 (public) or -1. */
static int read_state(int fd) {
    char c = 0;

    if (lseek(fd, 0, SEEK_SET) < 0 || read(fd, &c, 1) != 1) return -1;
    return c == '1' ? 1 : (c == '0' ? 0 : -1);
}

static void broadcast(int muted) {
    const char* action = muted ? "com.amazon.biscuit.service.MICROPHONE_MUTE_ON"
                               : "com.amazon.biscuit.service.MICROPHONE_MUTE_OFF";
    const char* const argv[] = {
        AM_BIN, "broadcast", "-n", "com.amazon.biscuit.service/.CommandReceiver",
        "-a", action, NULL,
    };
    pid_t pid = fork();

    if (pid == 0) {
        execv(argv[0], (char* const*)argv);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

int main(void) {
    int fd = open(PRIVACY_STATE, O_RDONLY | O_CLOEXEC);
    int last = -1;

    if (fd < 0) {
        fprintf(stderr, "biscuit-privacyd: %s: %s\n", PRIVACY_STATE, strerror(errno));
        return 1;
    }
    for (;;) {
        struct pollfd pfd = {.fd = fd, .events = POLLPRI | POLLERR};
        int state = read_state(fd);

        if (state >= 0 && state != last) {
            broadcast(state);
            last = state;
        }
        if (poll(&pfd, 1, POLL_MS) < 0 && errno != EINTR) return 1;
    }
}
