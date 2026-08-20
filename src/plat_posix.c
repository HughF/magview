/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of magview. magview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/*
 * plat_posix.c — Linux and macOS implementation of plat.h.
 *
 * Windows lives in plat_win32.c; the two are never compiled together.
 */
#if !defined(_WIN32)

#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void plat_utc_now(PlatUtc *out)
{
    if (!out)
        return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = tv.tv_sec;
    struct tm tm;
    gmtime_r(&t, &tm);

    out->year   = tm.tm_year + 1900;
    out->month  = tm.tm_mon + 1;
    out->day    = tm.tm_mday;
    out->hour   = tm.tm_hour;
    out->minute = tm.tm_min;
    out->second = tm.tm_sec;
    out->millis = (int)(tv.tv_usec / 1000);
}

/* ---- serial -------------------------------------------------------- */

struct PlatSerial { int fd; };

static speed_t baud_const(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

int plat_serial_list(PlatPortInfo *out, int max)
{
    int n = 0;

    /* /dev/serial/by-id gives stable, human-meaningful names — an FTDI
     * cable there reads as e.g. "usb-FTDI_USB-RS232_..." which is exactly
     * what the operator needs to pick the right instrument. */
    const char *by_id = "/dev/serial/by-id";
    DIR *d = opendir(by_id);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            if (e->d_name[0] == '.')
                continue;

            char link[512];
            snprintf(link, sizeof link, "%s/%s", by_id, e->d_name);

            char real[256];
            ssize_t l = readlink(link, real, sizeof real - 1);
            if (l > 0) {
                real[l] = '\0';
                const char *base = strrchr(real, '/');
                snprintf(out[n].path, sizeof out[n].path,
                         "/dev/%.240s", base ? base + 1 : real);
            } else {
                snprintf(out[n].path, sizeof out[n].path, "%.240s", link);
            }
            snprintf(out[n].label, sizeof out[n].label, "%.120s", e->d_name);
            n++;
        }
        closedir(d);
    }

    /* Fall back to raw device nodes for anything by-id did not cover. */
    static const char *prefixes[] = { "ttyUSB", "ttyACM", "cu.usbserial",
                                      "cu.usbmodem", "tty.usbserial", NULL };
    d = opendir("/dev");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < max) {
            for (int i = 0; prefixes[i]; i++) {
                if (strncmp(e->d_name, prefixes[i], strlen(prefixes[i])) != 0)
                    continue;

                char path[256];
                snprintf(path, sizeof path, "/dev/%.240s", e->d_name);

                bool dup = false;
                for (int j = 0; j < n; j++)
                    if (strcmp(out[j].path, path) == 0)
                        dup = true;
                if (dup)
                    break;

                snprintf(out[n].path, sizeof out[n].path, "%s", path);
                snprintf(out[n].label, sizeof out[n].label, "%.120s", e->d_name);
                n++;
                break;
            }
        }
        closedir(d);
    }

    return n;
}

PlatSerial *plat_serial_open(const char *path, int baud)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return NULL;

    struct termios t;
    if (tcgetattr(fd, &t) != 0) {
        close(fd);
        return NULL;
    }

    cfmakeraw(&t);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CRTSCTS;
    t.c_cflag &= ~CSTOPB;                     /* 8N1 */
    t.c_cflag &= ~PARENB;
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;

    cfsetispeed(&t, baud_const(baud));
    cfsetospeed(&t, baud_const(baud));

    if (tcsetattr(fd, TCSANOW, &t) != 0) {
        close(fd);
        return NULL;
    }
    tcflush(fd, TCIOFLUSH);

    PlatSerial *s = calloc(1, sizeof *s);
    if (!s) {
        close(fd);
        return NULL;
    }
    s->fd = fd;
    return s;
}

void plat_serial_close(PlatSerial *s)
{
    if (!s)
        return;
    close(s->fd);
    free(s);
}

int plat_serial_read(PlatSerial *s, void *buf, size_t cap)
{
    if (!s || cap == 0)
        return -1;

    ssize_t r = read(s->fd, buf, cap);
    if (r < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
               ? 0 : -1;
    /* A zero-byte read on a serial fd means "nothing yet", not EOF — unlike a
     * pipe. Losing the port shows up as an error instead. */
    return (int)r;
}

int plat_serial_write(PlatSerial *s, const void *buf, size_t len)
{
    if (!s)
        return -1;

    ssize_t w = write(s->fd, buf, len);
    if (w < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
               ? 0 : -1;
    return (int)w;
}

/* ---- filesystem ---------------------------------------------------- */

bool plat_config_dir(char *buf, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return false;

    if ((size_t)snprintf(buf, cap, "%s/.config", home) >= cap)
        return false;
    mkdir(buf, 0755);

    if ((size_t)snprintf(buf, cap, "%s/.config/magview", home) >= cap)
        return false;
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool plat_documents_dir(char *buf, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return false;

    /* Prefer ~/Documents if it exists, else fall back to the home dir. */
    char docs[512];
    if ((size_t)snprintf(docs, sizeof docs, "%s/Documents", home) < sizeof docs) {
        struct stat st;
        if (stat(docs, &st) == 0 && S_ISDIR(st.st_mode))
            return (size_t)snprintf(buf, cap, "%s", docs) < cap;
    }
    return (size_t)snprintf(buf, cap, "%s", home) < cap;
}

bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf)
{
    return (size_t)snprintf(buf, cap, "%s/%s", dir, leaf) < cap;
}

bool plat_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0)
        return true;
    return errno == EEXIST;
}

#endif /* !_WIN32 */
