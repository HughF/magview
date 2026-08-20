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
#include "mv_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct MvLog {
    FILE *csv;
    FILE *raw;
    long  rows;
    long  bytes;
};

MvLog *mv_log_open(const char *dir, const char *prefix, bool raw,
                   char *csv_path, size_t csv_cap, const char **err)
{
    if (csv_path && csv_cap)
        csv_path[0] = '\0';

    PlatUtc u;
    plat_utc_now(&u);

    char stamp[32];
    snprintf(stamp, sizeof stamp, "%04d%02d%02d_%02d%02d%02d",
             u.year, u.month, u.day, u.hour, u.minute, u.second);

    char leaf[128], path[512];
    snprintf(leaf, sizeof leaf, "%s_%s.csv", prefix, stamp);
    if (!plat_path_join(path, sizeof path, dir, leaf)) {
        if (err) *err = "path too long";
        return NULL;
    }

    FILE *csv = fopen(path, "w");
    if (!csv) {
        if (err) *err = "cannot create log file — check the folder";
        return NULL;
    }

    MvLog *l = calloc(1, sizeof *l);
    if (!l) {
        fclose(csv);
        if (err) *err = "out of memory";
        return NULL;
    }
    l->csv = csv;
    if (csv_path)
        snprintf(csv_path, csv_cap, "%s", path);

    fprintf(csv,
            "utc,tick_s,field_nT,signal,depth_m,altitude_m,quality,"
            "lat,lon\n");
    fflush(csv);

    if (raw) {
        char rleaf[128], rpath[512];
        snprintf(rleaf, sizeof rleaf, "%s_%s.raw", prefix, stamp);
        if (plat_path_join(rpath, sizeof rpath, dir, rleaf))
            l->raw = fopen(rpath, "w");   /* best effort; CSV is the record */
    }

    if (err) *err = NULL;
    return l;
}

void mv_log_close(MvLog *l)
{
    if (!l)
        return;
    if (l->csv) fclose(l->csv);
    if (l->raw) fclose(l->raw);
    free(l);
}

/* Print a value or an empty cell when it is not available. */
static void cell(FILE *f, double v, int dp)
{
    if (isfinite(v))
        fprintf(f, ",%.*f", dp, v);
    else
        fputc(',', f);
}

void mv_log_row(MvLog *l, const PlatUtc *utc, const MvSample *s)
{
    if (!l || !l->csv || !s)
        return;

    char ts[40];
    if (utc)
        snprintf(ts, sizeof ts, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 utc->year, utc->month, utc->day,
                 utc->hour, utc->minute, utc->second, utc->millis);
    else
        ts[0] = '\0';

    int n = fprintf(l->csv, "%s,%.3f", ts, s->tick);
    /* field */          n += fprintf(l->csv, ",%.3f", s->field_nt);
    cell(l->csv, s->signal, 1);
    cell(l->csv, s->depth_m, 2);
    cell(l->csv, s->altitude_m, 2);
    cell(l->csv, s->quality, 0);
    if (s->has_fix) {
        fprintf(l->csv, ",%.7f,%.7f\n", s->lat, s->lon);
    } else {
        fprintf(l->csv, ",,\n");
    }
    (void)n;

    fflush(l->csv);
    l->rows++;
    l->bytes = ftell(l->csv);
}

void mv_log_raw(MvLog *l, const char *line)
{
    if (!l || !l->raw || !line)
        return;
    fprintf(l->raw, "%s\n", line);
    /* Not flushed per line: the CSV is the guaranteed record; the raw file is
     * a bulk original and flushing it every line would double the I/O. */
}

long mv_log_rows(const MvLog *l)  { return l ? l->rows : 0; }
long mv_log_bytes(const MvLog *l) { return l ? l->bytes : 0; }
