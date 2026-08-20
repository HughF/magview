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
#include "mv_sim.h"
#include "mv_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAG_HZ      4.0
#define GPS_MS      1000
#define LAT0        50.36000
#define LON0        (-4.14000)
#define SPEED_MS    2.2          /* survey speed, m/s */
#define LEG_LEN_M   300.0
#define LINE_SPACE  25.0
#define BACKGROUND  50123.0      /* nT */

struct MvSim {
    uint64_t start_ms;
    long     mag_count;          /* mag lines emitted so far */
    long     gps_count;
    unsigned rng;
};

MvSim *mv_sim_create(void)
{
    MvSim *s = calloc(1, sizeof *s);
    if (s)
        s->rng = 0x1234abcdu;
    return s;
}

void mv_sim_destroy(MvSim *s) { free(s); }

static double frand(MvSim *s)   /* -1 .. 1 */
{
    s->rng = s->rng * 1103515245u + 12345u;
    return ((double)((s->rng >> 8) & 0xFFFF) / 32768.0) - 1.0;
}

/* Boat east/north (m) from the origin at survey time t seconds. */
static void boat_en(double t, double *east, double *north)
{
    double d = SPEED_MS * t;
    long leg = (long)floor(d / LEG_LEN_M);
    double along = d - leg * LEG_LEN_M;
    double e = (leg & 1) ? (LEG_LEN_M - along) : along;
    *east = e;
    *north = leg * LINE_SPACE;
}

/* Total field at an east/north position: background plus a handful of
 * dipole-ish anomalies, so the strips and the posted track have something
 * worth looking at. */
static double field_at(double e, double n)
{
    /* {east, north, amplitude nT, scale m} */
    static const double anom[][4] = {
        { 150,  12,  -60, 10 },
        { 240,  38,   90,  8 },
        {  70,  60,  -35, 14 },
        { 200,  86,  120,  6 },
    };
    double f = BACKGROUND;
    for (size_t i = 0; i < sizeof anom / sizeof anom[0]; i++) {
        double dx = e - anom[i][0], dy = n - anom[i][1];
        double r2 = (dx * dx + dy * dy) / (anom[i][3] * anom[i][3]);
        f += anom[i][2] / pow(1.0 + r2, 1.5);
    }
    return f;
}

static void en_to_ll(double e, double n, double *lat, double *lon)
{
    *lat = LAT0 + n / 111320.0;
    *lon = LON0 + e / (111320.0 * cos(LAT0 * M_PI / 180.0));
}

/* Append "*HH\r\n" NMEA checksum + terminator to a "$..." sentence in place. */
static void nmea_finish(char *line, size_t cap)
{
    unsigned cs = mv_nmea_checksum(line, strlen(line));
    size_t len = strlen(line);
    snprintf(line + len, cap - len, "*%02X\r\n", cs);
}

static int emit_gps(MvSim *s, long idx, char *buf, size_t cap)
{
    double t = idx;                     /* one GPS per second */
    double e, n, lat, lon;
    boat_en(t, &e, &n);
    en_to_ll(e, n, &lat, &lon);

    int hh = (int)(t / 3600) % 24, mm = (int)(t / 60) % 60, ss = (int)t % 60;

    double alat = fabs(lat), alon = fabs(lon);
    int ladeg = (int)alat, lodeg = (int)alon;
    double lamin = (alat - ladeg) * 60.0, lomin = (alon - lodeg) * 60.0;

    char body[160];
    snprintf(body, sizeof body,
             "$GPGGA,%02d%02d%02d.00,%02d%07.4f,%c,%03d%07.4f,%c,1,08,0.9,"
             "10.0,M,47.0,M,,",
             hh, mm, ss,
             ladeg, lamin, lat >= 0 ? 'N' : 'S',
             lodeg, lomin, lon >= 0 ? 'E' : 'W');
    nmea_finish(body, sizeof body);
    return snprintf(buf, cap, "%s", body);
}

static int emit_mag(MvSim *s, long idx, char *buf, size_t cap)
{
    double t = idx / MAG_HZ;
    double e, n;
    boat_en(t, &e, &n);

    double field = field_at(e, n) + frand(s) * 0.25;
    double depth = 8.0 + 1.5 * sin(t / 30.0) + frand(s) * 0.05;
    double sig   = 1050 + frand(s) * 40;
    double alt   = 22.0 + 2.0 * cos(t / 25.0);
    int    q     = 5 + (int)((sig - 1000) / 100);
    if (q < 0) q = 0;
    if (q > 99) q = 99;

    int hh = (int)(t / 3600) % 24, mm = (int)(t / 60) % 60;
    double ss = fmod(t, 60.0);

    return snprintf(buf, cap,
        "*26.232/%02d:%02d:%04.1f F:%.3f S:%03.0f D:+%05.1fm A:%04.1f "
        "L:0 250ms_Q:%02d !!!!\r\n",
        hh, mm, ss, field, sig, depth, alt, q);
}

int mv_sim_read(MvSim *s, uint64_t now_ms, char *buf, size_t cap)
{
    if (!s || !buf || cap < 128)
        return 0;

    if (s->start_ms == 0)
        s->start_ms = now_ms ? now_ms : 1;

    double elapsed = (double)(now_ms - s->start_ms) / 1000.0;
    long due_mag = (long)(elapsed * MAG_HZ) + 1;
    long due_gps = (long)(elapsed * 1000.0 / GPS_MS) + 1;

    int used = 0;
    /* Interleave in rough time order: a GPS at each second boundary, then the
     * mag readings up to now. Bounded per call by cap. */
    bool progress = true;
    while (progress && (size_t)used + 200 < cap) {
        progress = false;

        if (s->gps_count < due_gps &&
            (double)s->gps_count <= s->mag_count / MAG_HZ) {
            used += emit_gps(s, s->gps_count++, buf + used, cap - used);
            progress = true;
            continue;
        }
        if (s->mag_count < due_mag) {
            used += emit_mag(s, s->mag_count++, buf + used, cap - used);
            progress = true;
        }
    }
    return used;
}
