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
 * plat.h — everything OS-specific, behind one header.
 *
 * No file above this line calls an OS API directly. The serial link here is
 * non-blocking: the app polls it once per frame, so nothing in the program
 * can stall the window. plat_posix.c covers Linux and macOS; plat_win32.c
 * covers Windows. The two are never compiled together.
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- time ---------------------------------------------------------- */

uint64_t plat_now_ms(void);            /* monotonic, arbitrary origin */

/* Wall-clock, broken out in UTC, for log filenames and timestamps. */
typedef struct {
    int year, month, day, hour, minute, second, millis;
} PlatUtc;

void plat_utc_now(PlatUtc *out);

/* ---- serial -------------------------------------------------------- */

typedef struct PlatSerial PlatSerial;

#define PLAT_MAX_PORTS 32

typedef struct {
    char path[256];                    /* what plat_serial_open() wants  */
    char label[128];                   /* what the operator should see   */
} PlatPortInfo;

/* Enumerate candidate serial ports. Returns the count, at most max. */
int  plat_serial_list(PlatPortInfo *out, int max);

/* Open at `baud`, 8N1, non-blocking. NULL on failure. */
PlatSerial *plat_serial_open(const char *path, int baud);
void plat_serial_close(PlatSerial *s);

/* Non-blocking. Return bytes moved, 0 for "nothing right now", or -1 on a
 * hard error (the caller should close and go back to disconnected). */
int  plat_serial_read(PlatSerial *s, void *buf, size_t cap);
int  plat_serial_write(PlatSerial *s, const void *buf, size_t len);

/* ---- filesystem ---------------------------------------------------- */

/* Per-user config directory, created if needed. False if it cannot be made. */
bool plat_config_dir(char *buf, size_t cap);

/* Documents (or home) directory — a sensible default place to log to. */
bool plat_documents_dir(char *buf, size_t cap);

/* Join with the platform separator. False if it would not fit. */
bool plat_path_join(char *buf, size_t cap, const char *dir, const char *leaf);

/* Create a directory (and its parents are assumed to exist). Idempotent:
 * true if the directory now exists. */
bool plat_mkdir(const char *path);

#endif /* PLAT_H */
