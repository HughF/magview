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
#include "mv_app.h"
#include "mv_proto.h"
#include "mv_log.h"
#include "mv_sim.h"
#include "mv_geo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct MvApp {
    MvState     st;

    PlatSerial *serial;
    MvSim      *sim;

    MvLineAsm   asm_;
    uint64_t    session_ms;        /* monotonic origin for sample ticks    */

    MvSample    samples[MV_SAMPLE_CAP];
    int         n;

    MvSample    track[MV_TRACK_CAP];
    int         track_n;

    /* console ring */
    char        console[MV_CONSOLE_LINES][MV_CONSOLE_WIDTH];
    int         con_head;          /* index of oldest                      */
    int         con_n;

    /* last GPS fix */
    double      fix_lat, fix_lon;
    bool        have_fix;

    /* rate estimate */
    double      dt_ema;

    MvLog      *log;
};

/* ------------------------------------------------------------------ */

static void console_push(MvApp *a, const char *line)
{
    int slot;
    if (a->con_n < MV_CONSOLE_LINES) {
        slot = (a->con_head + a->con_n) % MV_CONSOLE_LINES;
        a->con_n++;
    } else {
        slot = a->con_head;
        a->con_head = (a->con_head + 1) % MV_CONSOLE_LINES;
    }
    snprintf(a->console[slot], MV_CONSOLE_WIDTH, "%s", line);
}

static void samples_push(MvApp *a, const MvSample *s)
{
    if (a->n >= MV_SAMPLE_CAP) {
        /* Drop the oldest quarter in one move rather than shift by one on
         * every reading — cheaper, and the strips only ever show a window. */
        int drop = MV_SAMPLE_CAP / 4;
        memmove(a->samples, a->samples + drop,
                (size_t)(a->n - drop) * sizeof(MvSample));
        a->n -= drop;
    }
    a->samples[a->n++] = *s;
}

static void track_push(MvApp *a, const MvSample *s)
{
    if (a->track_n > 0) {
        const MvSample *last = &a->track[a->track_n - 1];
        double d = mv_geo_distance_m(last->lat, last->lon, s->lat, s->lon);
        if (d < MV_TRACK_MIN_M)
            return;               /* not moved enough to add a vertex */
    }
    if (a->track_n >= MV_TRACK_CAP) {
        int drop = MV_TRACK_CAP / 4;
        memmove(a->track, a->track + drop,
                (size_t)(a->track_n - drop) * sizeof(MvSample));
        a->track_n -= drop;
    }
    a->track[a->track_n++] = *s;
}

static void on_line(void *user, const char *line, size_t len)
{
    MvApp *a = user;

    console_push(a, line);
    if (a->log)
        mv_log_raw(a->log, line);

    MvMsg m;
    if (!mv_parse_line(line, len, &m)) {
        a->st.status.bad_lines++;
        return;
    }
    if (m.checksum_present && !m.checksum_ok)
        a->st.status.bad_checksum++;

    uint64_t now = plat_now_ms();

    if (m.kind == MV_MSG_GPS) {
        a->fix_lat = m.lat;
        a->fix_lon = m.lon;
        a->have_fix = true;
        a->st.last_fix_ms = now;
        a->st.status.has_fix = true;
        a->st.status.lat = m.lat;
        a->st.status.lon = m.lon;
        return;
    }

    /* MV_MSG_MAG */
    MvSample s;
    memset(&s, 0, sizeof s);
    s.ms       = now;
    s.tick     = (double)(now - a->session_ms) / 1000.0;
    s.field_nt = m.field_nt;
    s.signal   = m.signal;
    s.depth_m  = m.depth_m;
    s.altitude_m = m.altitude_m;
    s.quality  = m.quality;
    s.leak     = m.leak;
    s.has_leak = m.has_leak;
    s.lat = s.lon = MV_NO_FIX;
    if (a->have_fix) {
        s.lat = a->fix_lat;
        s.lon = a->fix_lon;
        s.has_fix = true;
    }

    /* rate: EMA of the interval between mag readings */
    if (a->st.last_data_ms) {
        double dt = (double)(now - a->st.last_data_ms) / 1000.0;
        if (dt > 0 && dt < 10.0)
            a->dt_ema = (a->dt_ema > 0) ? a->dt_ema * 0.8 + dt * 0.2 : dt;
    }
    a->st.last_data_ms = now;

    samples_push(a, &s);
    if (s.has_fix)
        track_push(a, &s);

    /* live status snapshot */
    MvStatus *st = &a->st.status;
    st->have_data  = true;
    st->field_nt   = s.field_nt;
    st->signal     = s.signal;
    st->depth_m    = s.depth_m;
    st->altitude_m = s.altitude_m;
    st->quality    = s.quality;
    st->leak       = s.leak;
    st->has_leak   = s.has_leak;
    st->rate_hz    = (a->dt_ema > 0) ? 1.0 / a->dt_ema : 0.0;

    if (a->log) {
        PlatUtc u;
        plat_utc_now(&u);
        mv_log_row(a->log, &u, &s);
        a->st.log_rows  = mv_log_rows(a->log);
        a->st.log_bytes = mv_log_bytes(a->log);
    }
}

/* ------------------------------------------------------------------ */

MvApp *mv_app_create(bool simulate)
{
    MvApp *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;

    a->session_ms = plat_now_ms();
    mv_lineasm_reset(&a->asm_);
    a->st.simulate = simulate;
    a->st.baud = 9600;

    if (simulate) {
        a->sim = mv_sim_create();
        a->st.link = MV_LINK_OPEN;
        snprintf(a->st.port, sizeof a->st.port, "simulator");
    }
    return a;
}

void mv_app_destroy(MvApp *a)
{
    if (!a)
        return;
    if (a->log)    mv_log_close(a->log);
    if (a->serial) plat_serial_close(a->serial);
    if (a->sim)    mv_sim_destroy(a->sim);
    free(a);
}

void mv_app_poll(MvApp *a)
{
    if (!a)
        return;

    char buf[4096];

    if (a->sim) {
        int r = mv_sim_read(a->sim, plat_now_ms(), buf, sizeof buf);
        if (r > 0)
            mv_lineasm_feed(&a->asm_, buf, (size_t)r, on_line, a);
    } else if (a->serial) {
        for (int guard = 0; guard < 16; guard++) {      /* drain, bounded */
            int r = plat_serial_read(a->serial, buf, sizeof buf);
            if (r < 0) {
                snprintf(a->st.error, sizeof a->st.error,
                         "lost the serial port %.120s", a->st.port);
                a->st.link = MV_LINK_ERROR;
                plat_serial_close(a->serial);
                a->serial = NULL;
                break;
            }
            if (r == 0)
                break;
            mv_lineasm_feed(&a->asm_, buf, (size_t)r, on_line, a);
            if ((size_t)r < sizeof buf)
                break;
        }
    }

    /* Fix goes stale when the GPS stops refreshing it. */
    uint64_t now = plat_now_ms();
    if (a->st.status.has_fix &&
        now - a->st.last_fix_ms > MV_STALE_MS) {
        /* keep the position but mark it not-current via the UI's stale check */
    }
}

const MvState *mv_app_state(const MvApp *a) { return a ? &a->st : NULL; }

const MvSample *mv_app_samples(const MvApp *a, int *n)
{
    if (n) *n = a ? a->n : 0;
    return a ? a->samples : NULL;
}

const MvSample *mv_app_track(const MvApp *a, int *n)
{
    if (n) *n = a ? a->track_n : 0;
    return a ? a->track : NULL;
}

int mv_app_console_count(const MvApp *a) { return a ? a->con_n : 0; }

const char *mv_app_console_line(const MvApp *a, int i)
{
    if (!a || i < 0 || i >= a->con_n)
        return "";
    return a->console[(a->con_head + i) % MV_CONSOLE_LINES];
}

/* ------------------------------------------------------------------ */

bool mv_app_connect(MvApp *a, const char *port, int baud)
{
    if (!a || a->sim)
        return false;

    mv_app_disconnect(a);

    a->serial = plat_serial_open(port, baud);
    if (!a->serial) {
        snprintf(a->st.error, sizeof a->st.error,
                 "could not open %s at %d baud", port, baud);
        a->st.link = MV_LINK_ERROR;
        return false;
    }

    snprintf(a->st.port, sizeof a->st.port, "%s", port);
    a->st.baud = baud;
    a->st.error[0] = '\0';
    a->st.link = MV_LINK_OPEN;
    mv_lineasm_reset(&a->asm_);
    return true;
}

void mv_app_disconnect(MvApp *a)
{
    if (!a || a->sim)
        return;
    if (a->serial) {
        plat_serial_close(a->serial);
        a->serial = NULL;
    }
    a->st.link = MV_LINK_CLOSED;
    a->st.port[0] = '\0';
}

void mv_app_send(MvApp *a, const char *line)
{
    if (!a || !a->serial || !line)
        return;
    char out[256];
    int n = snprintf(out, sizeof out, "%s\r\n", line);
    if (n > 0)
        plat_serial_write(a->serial, out, (size_t)n);
}

const char *mv_app_log_start(MvApp *a, const char *dir, bool raw)
{
    if (!a)
        return "no app";
    if (a->log)
        return NULL;                 /* already logging */

    const char *err = NULL;
    a->log = mv_log_open(dir, "magview", raw,
                         a->st.log_path, sizeof a->st.log_path, &err);
    if (!a->log)
        return err ? err : "could not start the log";

    a->st.logging = true;
    a->st.log_rows = 0;
    a->st.log_bytes = 0;
    return NULL;
}

void mv_app_log_stop(MvApp *a)
{
    if (!a || !a->log)
        return;
    mv_log_close(a->log);
    a->log = NULL;
    a->st.logging = false;
}

void mv_app_clear(MvApp *a)
{
    if (!a)
        return;
    a->n = 0;
    a->track_n = 0;
    a->con_n = 0;
    a->con_head = 0;
    a->have_fix = false;
    a->dt_ema = 0;
    memset(&a->st.status, 0, sizeof a->st.status);
}
