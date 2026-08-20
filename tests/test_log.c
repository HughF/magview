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
 * The point of this test is the requirement in the brief: the log must hold
 * mag data and be georeferenced. It writes a row with a fix and one without,
 * reads the file back, and checks the field value and the position are both
 * present on the fixed row.
 */
#include "mv_test.h"
#include "mv_log.h"
#include "plat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, needle) != NULL;
}

TEST_MAIN("test_log",
    /* A private scratch dir under the config dir. */
    char dir[512];
    CHECK(plat_config_dir(dir, sizeof dir));

    char csv[512];
    const char *err = NULL;
    MvLog *l = mv_log_open(dir, "unittest", false, csv, sizeof csv, &err);
    CHECK(l != NULL);
    CHECK(err == NULL);
    CHECK(csv[0] != '\0');

    PlatUtc u = { 2026, 8, 20, 12, 0, 0, 0 };

    /* A reading with a fix: mag data plus a position. */
    MvSample s;
    memset(&s, 0, sizeof s);
    s.tick = 1.25;
    s.field_nt = 50123.456;
    s.signal = 1050;
    s.depth_m = 8.4;
    s.altitude_m = 22.0;
    s.quality = 7;
    s.lat = 50.3600000;
    s.lon = -4.1400000;
    s.has_fix = true;
    mv_log_row(l, &u, &s);

    /* A reading with no fix: still logged, position cells empty. */
    MvSample s2 = s;
    s2.tick = 1.50;
    s2.field_nt = 50130.0;
    s2.has_fix = false;
    s2.lat = s2.lon = MV_NO_FIX;
    mv_log_row(l, &u, &s2);

    CHECK(mv_log_rows(l) == 2);
    mv_log_close(l);

    /* The file carries the mag field value ... */
    CHECK(file_contains(csv, "50123.456"));
    /* ... and the georeference on the same kind of row ... */
    CHECK(file_contains(csv, "50.3600000"));
    CHECK(file_contains(csv, "-4.1400000"));
    /* ... and the CSV header names the mag + position columns. */
    CHECK(file_contains(csv, "field_nT"));
    CHECK(file_contains(csv, "lat,lon"));

    remove(csv);
)
