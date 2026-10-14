/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * biscuit-keyd: Biscuit's microphone button (KEY_MUTE on the MTK keypad)
 * toggles the microphone. Android treats KEYCODE_MUTE as a media key, so
 * forward presses to BiscuitService, which calls setMicrophoneMute().
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#ifndef AM_BIN
#define AM_BIN "/system/bin/am"
#endif
#define MAX_DEVICES 16
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define TEST_BIT(bit, array) (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

static const char* const kToggleCmd[] = {
    AM_BIN, "broadcast", "-n", "com.amazon.biscuit.service/.CommandReceiver",
    "-a", "com.amazon.biscuit.service.MICROPHONE_MUTE_TOGGLE", NULL,
};

static int has_key_mute(int fd) {
    unsigned long keys[KEY_MAX / BITS_PER_LONG + 1];

    memset(keys, 0, sizeof(keys));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return 0;
    return TEST_BIT(KEY_MUTE, keys);
}

static int open_mute_devices(struct pollfd* fds) {
    DIR* dir = opendir(INPUT_DIR);
    struct dirent* de;
    int count = 0;

    if (!dir) {
        fprintf(stderr, "biscuit-keyd: %s: %s\n", INPUT_DIR, strerror(errno));
        return 0;
    }
    while ((de = readdir(dir)) && count < MAX_DEVICES) {
        char path[PATH_MAX];
        int fd;

        if (strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), INPUT_DIR "/%s", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        if (!has_key_mute(fd)) {
            close(fd);
            continue;
        }
        fds[count].fd = fd;
        fds[count].events = POLLIN;
        count++;
    }
    closedir(dir);
    return count;
}

static void toggle_microphone(void) {
    pid_t pid = fork();

    if (pid == 0) {
        execv(kToggleCmd[0], (char* const*)kToggleCmd);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

int main(void) {
    struct pollfd fds[MAX_DEVICES];
    int count = open_mute_devices(fds);

    if (count == 0) {
        fprintf(stderr, "biscuit-keyd: no input device reports KEY_MUTE\n");
        return 1;
    }
    for (;;) {
        int i;

        if (poll(fds, count, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        for (i = 0; i < count; i++) {
            struct input_event ev;

            if (!(fds[i].revents & POLLIN)) continue;
            if (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev) && ev.type == EV_KEY &&
                ev.code == KEY_MUTE && ev.value == 1) {
                toggle_microphone();
            }
        }
    }
}
