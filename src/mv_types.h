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
 * mv_types.h — data types shared across the core.
 *
 * Nothing here knows about SDL, sockets or files. Fixed-size buffers
 * throughout; no core struct owns a pointer it has to free.
 *
 * A value of MV_NA marks a numeric field the instrument did not report — the
 * Explorer's several output formats carry different subsets, so depth,
 * altitude and quality are all optional and the UI must not print a zero
 * where there was simply no reading.
 */
#ifndef MV_TYPES_H
#define MV_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

/* "Not available" sentinel for optional numeric fields. NAN, so any arithmetic
 * on it stays NAN and range checks (isfinite) reject it without a magic
 * comparison scattered through the code. */
#define MV_NA  (NAN)

/* A latitude or longitude of 999 means "no fix", matching the rest of the
 * toolchain's convention (see mv_geo.h). */
#define MV_NO_FIX 999.0

static inline bool mv_has(double v) { return isfinite(v); }

/* ------------------------------------------------------------------ */
/* One reading                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    double   tick;         /* seconds since the session started         */
    uint64_t ms;           /* monotonic arrival time                    */

    double   field_nt;     /* total magnetic field, nanoTesla           */
    double   signal;       /* signal strength (raw); >800 ok, >1300 good */
    double   depth_m;      /* towfish depth, metres      (MV_NA if none) */
    double   altitude_m;   /* altitude above seabed, m   (MV_NA if none) */
    double   quality;      /* quality indicator          (MV_NA if none) */
    bool     leak;         /* leak sensor tripped                       */
    bool     has_leak;

    /* Position, merged from the GPS stream at the moment this reading
     * arrived. MV_NO_FIX until a fix has been seen. */
    double   lat, lon;
    bool     has_fix;
} MvSample;

/* ------------------------------------------------------------------ */
/* Parsed line                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    MV_MSG_NONE = 0,
    MV_MSG_MAG,            /* a magnetometer data line                  */
    MV_MSG_GPS             /* an NMEA GPS fix ($GPGGA / $GPRMC / $GPGLL) */
} MvMsgKind;

typedef struct {
    MvMsgKind kind;

    /* --- MV_MSG_MAG --- */
    double field_nt;
    double signal;
    double depth_m;        /* MV_NA when the format carried none        */
    double altitude_m;     /* MV_NA when absent                         */
    double quality;        /* MV_NA when absent                         */
    bool   leak;
    bool   has_leak;

    /* Instrument timestamp, if the tagged format carried one. */
    bool   have_time;
    int    hour, minute, second, millis;

    /* --- MV_MSG_GPS --- */
    double lat, lon;       /* decimal degrees                           */

    /* --- integrity (NMEA-style *HH checksum, when present) --- */
    bool   checksum_present;
    bool   checksum_ok;
} MvMsg;

/* ------------------------------------------------------------------ */
/* Live instrument status, derived from the stream                     */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     have_data;        /* at least one mag line parsed          */
    double   field_nt;         /* latest                                */
    double   signal;
    double   depth_m;
    double   altitude_m;
    double   quality;
    bool     leak;
    bool     has_leak;
    double   rate_hz;          /* measured sample rate                  */

    bool     has_fix;
    double   lat, lon;

    int      bad_lines;        /* running count of unparseable lines    */
    int      bad_checksum;     /* running count of failed checksums     */
} MvStatus;

#endif /* MV_TYPES_H */
