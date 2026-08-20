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
 * mv_app.h — application state and instrument session
 *
 * Single-threaded. The serial link is non-blocking and polled once per frame
 * by mv_app_poll(), so there is no shared mutable state and no lock anywhere
 * in the program. At 9600 baud a frame's worth of data is a few dozen bytes,
 * so a thread would buy nothing and cost a class of races the program cannot
 * afford.
 */
#ifndef MV_APP_H
#define MV_APP_H

#include "mv_types.h"
#include "mv_config.h"
#include "plat.h"
#include <stddef.h>

#define MV_SAMPLE_CAP    20000   /* readings kept for the strips           */
#define MV_TRACK_CAP      6000   /* positioned readings for the plotter    */
#define MV_CONSOLE_LINES   400
#define MV_CONSOLE_WIDTH   256
#define MV_TRACK_MIN_M     1.0    /* min move to add a track vertex, m      */
#define MV_STALE_MS        5000   /* live position older than this is stale */

typedef enum {
    MV_LINK_CLOSED = 0,
    MV_LINK_OPEN,             /* port open; data may or may not be flowing */
    MV_LINK_ERROR
} MvLinkState;

typedef struct MvApp MvApp;

typedef struct {
    MvLinkState link;
    char        port[256];
    int         baud;
    char        error[160];        /* last failure, "" when clear         */

    MvStatus    status;
    uint64_t    last_data_ms;      /* when the last mag line arrived       */
    uint64_t    last_fix_ms;

    bool        logging;
    char        log_path[512];
    long        log_rows;
    long        log_bytes;

    /* GPS input source (mv_config.h). For MV_GPS_INTERLEAVED the fix comes off
     * the mag line; the other two open a dedicated source. */
    int         gps_source;
    char        gps_port[256];
    int         gps_baud;
    int         gps_udp_port;
    bool        gps_link_open;     /* a dedicated source is open           */
    char        gps_error[160];    /* why a dedicated source failed        */
    int         gps_sentences;     /* fixes seen from the dedicated source */

    bool        simulate;
} MvState;

MvApp *mv_app_create(bool simulate);
void   mv_app_destroy(MvApp *a);

/* Once per frame. Cheap when idle. */
void   mv_app_poll(MvApp *a);

const MvState *mv_app_state(const MvApp *a);

/* Readings, oldest first, for the strip plots. Returns the pointer and sets
 * *n. The array is stable until the next poll. */
const MvSample *mv_app_samples(const MvApp *a, int *n);

/* Positioned readings for the plotter, oldest first. */
const MvSample *mv_app_track(const MvApp *a, int *n);

/* Raw serial console, newest last. */
int         mv_app_console_count(const MvApp *a);
const char *mv_app_console_line(const MvApp *a, int i);

/* ---- operator actions --------------------------------------------- */

bool mv_app_connect(MvApp *a, const char *port, int baud);
void mv_app_disconnect(MvApp *a);

/* Send a line to the instrument (a trailing CRLF is added). No-op if the port
 * is closed. Lets the operator issue tune / run / stop commands by hand. */
void mv_app_send(MvApp *a, const char *line);

/* Start/stop logging. start returns NULL or a static failure reason. */
const char *mv_app_log_start(MvApp *a, const char *dir, bool raw);
void        mv_app_log_stop(MvApp *a);

/*
 * Choose where the GPS comes from and persist the choice. `source` is an
 * MvGpsSource; `port`/`baud` apply to MV_GPS_SERIAL, `udp_port` to
 * MV_GPS_UDP. Reopens the dedicated source (or closes it for INTERLEAVED).
 * Returns NULL or a static failure reason.
 */
const char *mv_app_set_gps(MvApp *a, int source, const char *port,
                           int baud, int udp_port);

/* Forget all buffered readings and the track (does not touch the log). */
void mv_app_clear(MvApp *a);

#endif /* MV_APP_H */
