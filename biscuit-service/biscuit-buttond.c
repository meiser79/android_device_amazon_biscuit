/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * biscuit-buttond: Biscuit's two buttons for Android.
 *
 * Microphone button: the kernel (amz_priv) mutes the microphones itself when
 * it is pressed. Follow its privacy_state and tell BiscuitService, so
 * Android's microphone mute state matches the hardware.
 *
 * Action button: send every press and release (KEY_HELP on the keypad) as a
 * com.amazon.biscuit.service.ACTION_BUTTON broadcast with "down" and
 * "eventTime", also while Android sleeps, so a headless kiosk hears it
 * without an accessibility service (ro.biscuit.action_broadcast=1 advertises
 * it). The key is not grabbed: Android still handles it as before.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PRIVACY_STATE
#define PRIVACY_STATE "/dev/biscuit_privacy/privacy_state"
#endif
#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#ifndef AM_BIN
#define AM_BIN "/system/bin/am"
#endif
/* Re-read now and then in case a sysfs_notify() is missed. */
#ifndef POLL_MS
#define POLL_MS 5000
#endif

#define BITS_PER_LONG (8 * sizeof(unsigned long))

static void run(const char* const argv[]) {
    pid_t pid = fork();

    if (pid == 0) {
        execv(argv[0], (char* const*)argv);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

/* Returns 1 (private), 0 (public) or -1. */
static int read_state(int fd) {
    char c = 0;

    if (lseek(fd, 0, SEEK_SET) < 0 || read(fd, &c, 1) != 1) return -1;
    return c == '1' ? 1 : (c == '0' ? 0 : -1);
}

static void broadcast_privacy(int muted) {
    const char* action = muted ? "com.amazon.biscuit.service.MICROPHONE_MUTE_ON"
                               : "com.amazon.biscuit.service.MICROPHONE_MUTE_OFF";
    const char* const argv[] = {
        AM_BIN, "broadcast", "-n", "com.amazon.biscuit.service/.CommandReceiver",
        "-a", action, NULL,
    };

    run(argv);
}

/*
 * As PhoneWindowManager sent it on CM14: to registered receivers of all
 * users, in the foreground queue (FLAG_RECEIVER_FOREGROUND |
 * FLAG_RECEIVER_REGISTERED_ONLY). On Android 11 am is cmd activity, which
 * takes about 30 ms.
 */
static void broadcast_action(int down, long long event_ms) {
    char ms[24];
    const char* const argv[] = {
        AM_BIN, "broadcast", "-a", "com.amazon.biscuit.service.ACTION_BUTTON",
        "--ez", "down", down ? "true" : "false", "--el", "eventTime", ms,
        "-f", "0x50000000", "--user", "all", NULL,
    };

    snprintf(ms, sizeof(ms), "%lld", event_ms);
    run(argv);
}

#ifndef KEYPAD_PATH
static int has_key(int fd, int code) {
    unsigned long bits[KEY_MAX / BITS_PER_LONG + 1];

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return 0;
    return (bits[code / BITS_PER_LONG] >> (code % BITS_PER_LONG)) & 1;
}
#endif

/* The input device with the Action button (mtk-kpd), or -1. */
static int open_keypad(void) {
#ifdef KEYPAD_PATH
    /* Host tests: a FIFO that carries struct input_event. */
    return open(KEYPAD_PATH, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
#else
    DIR* dir = opendir(INPUT_DIR);
    struct dirent* de;
    int found = -1;
    /* Android's uptime clock, the base of KeyEvent.getEventTime(). */
    int clock = CLOCK_MONOTONIC;

    if (!dir) return -1;
    while (found < 0 && (de = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        int fd;

        if (strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) continue;
        if (has_key(fd, KEY_HELP))
            found = fd;
        else
            close(fd);
    }
    closedir(dir);
    if (found >= 0) ioctl(found, EVIOCSCLOCKID, &clock);
    return found;
#endif
}

/* Returns -1 once the keypad is gone. */
static int read_keypad(int fd) {
    struct input_event ev;
    ssize_t n;

    while ((n = read(fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        /* 1 is a press and 0 a release; 2 is autorepeat, which CM14 skipped. */
        if (ev.type != EV_KEY || ev.code != KEY_HELP || ev.value == 2) continue;
        broadcast_action(ev.value, (long long)ev.time.tv_sec * 1000 + ev.time.tv_usec / 1000);
    }
    return n < 0 && errno != EAGAIN && errno != EINTR ? -1 : 0;
}

int main(void) {
    int privacy = open(PRIVACY_STATE, O_RDONLY | O_CLOEXEC);
    int keypad = open_keypad();
    int ep = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    int last = -1;

    if (privacy < 0)
        fprintf(stderr, "biscuit-buttond: %s: %s\n", PRIVACY_STATE, strerror(errno));
    if (keypad < 0) fprintf(stderr, "biscuit-buttond: no input device with KEY_HELP\n");
    if ((privacy < 0 && keypad < 0) || ep < 0) return 1;

    if (privacy >= 0) {
        /* sysfs_notify() wakes it; if it cannot be watched, POLL_MS still re-reads it. */
        ev.events = EPOLLPRI | EPOLLERR;
        ev.data.fd = privacy;
        epoll_ctl(ep, EPOLL_CTL_ADD, privacy, &ev);
    }
    if (keypad >= 0) {
        /* Keeps the device awake until the next epoll_wait(), when the
         * broadcast is out (needs CAP_BLOCK_SUSPEND, else it is ignored). */
        ev.events = EPOLLIN | EPOLLWAKEUP;
        ev.data.fd = keypad;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, keypad, &ev) < 0) return 1;
    }

    for (;;) {
        struct epoll_event events[2];
        int i, n;

        if (privacy >= 0) {
            int state = read_state(privacy);

            if (state >= 0 && state != last) {
                broadcast_privacy(state);
                last = state;
            }
        }
        n = epoll_wait(ep, events, 2, POLL_MS);
        if (n < 0 && errno != EINTR) return 1;
        for (i = 0; i < n; i++) {
            if (events[i].data.fd != keypad) continue;
            if (read_keypad(keypad) < 0 || (events[i].events & (EPOLLERR | EPOLLHUP))) {
                fprintf(stderr, "biscuit-buttond: keypad gone\n");
                epoll_ctl(ep, EPOLL_CTL_DEL, keypad, NULL);
                close(keypad);
                keypad = -1;
                if (privacy < 0) return 1;
            }
        }
    }
}
