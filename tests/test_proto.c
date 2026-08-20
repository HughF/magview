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
#include "mv_test.h"
#include "mv_proto.h"

#include <string.h>

static bool parse(const char *s, MvMsg *m)
{
    return mv_parse_line(s, strlen(s), m);
}

/* ---- line assembler capture ---- */
static char cap_lines[16][256];
static int  cap_n;
static void cap_cb(void *user, const char *line, size_t len)
{
    (void)user; (void)len;
    if (cap_n < 16)
        snprintf(cap_lines[cap_n++], sizeof cap_lines[0], "%s", line);
}

TEST_MAIN("test_proto",
    MvMsg m;

    /* ---- tagged format (what the simulator emits) ---- */
    CHECK(parse("*26.232/01:02:03.4 F:50123.000 S:1050 D:+008.0m A:022.0 "
                "L:0 250ms_Q:05 !!!!", &m));
    CHECK(m.kind == MV_MSG_MAG);
    CHECK_NEAR(m.field_nt, 50123.0, 1e-3);
    CHECK_NEAR(m.signal, 1050.0, 1e-6);
    CHECK_NEAR(m.depth_m, 8.0, 1e-6);
    CHECK_NEAR(m.altitude_m, 22.0, 1e-6);
    CHECK_NEAR(m.quality, 5.0, 1e-6);
    CHECK(m.has_leak && !m.leak);
    CHECK(m.have_time && m.hour == 1 && m.minute == 2 && m.second == 3);

    /* a leak flag of 1 is read as tripped */
    CHECK(parse("*26.232/00:00:00.0 F:49000.0 S:900 D:+010.0m L:1 Q:07", &m));
    CHECK(m.has_leak && m.leak);

    /* ---- legacy '$' format, field below 100000 (space flag) ---- */
    CHECK(parse("$ 51234.56  1024  152", &m));
    CHECK(m.kind == MV_MSG_MAG);
    CHECK_NEAR(m.field_nt, 51234.56, 1e-4);
    CHECK_NEAR(m.signal, 1024.0, 1e-6);
    CHECK_NEAR(m.depth_m, 15.2, 1e-6);        /* reported in 0.1 m */

    /* legacy, field at/above 100000 nT: the flag is '1', kept as the
     * hundred-thousands digit */
    CHECK(parse("$151234.56 1024 152", &m));
    CHECK_NEAR(m.field_nt, 151234.56, 1e-4);

    /* legacy with only field + signal, no depth => depth is NA, not zero */
    CHECK(parse("$ 48000.00 950", &m));
    CHECK_NEAR(m.field_nt, 48000.0, 1e-4);
    CHECK(!mv_has(m.depth_m));

    /* ---- NMEA GPS, canonical examples with known checksums ---- */
    CHECK(parse("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,"
                "46.9,M,,*47", &m));
    CHECK(m.kind == MV_MSG_GPS);
    CHECK(m.checksum_present && m.checksum_ok);
    CHECK_NEAR(m.lat, 48.11730, 1e-4);
    CHECK_NEAR(m.lon, 11.51667, 1e-4);

    CHECK(parse("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
                "230394,003.1,W*6A", &m));
    CHECK(m.kind == MV_MSG_GPS);
    CHECK(m.checksum_present && m.checksum_ok);
    CHECK_NEAR(m.lat, 48.11730, 1e-4);
    CHECK_NEAR(m.lon, 11.51667, 1e-4);

    /* southern / western hemispheres carry the sign */
    CHECK(parse("$GPGGA,120000,5021.600,S,00408.400,W,1,08,0.9,10,M,47,M,,*hh",
                &m));
    CHECK_NEAR(m.lat, -50.36000, 1e-4);
    CHECK_NEAR(m.lon,  -4.14000, 1e-4);

    /* a GGA with fix quality 0 is not a usable fix */
    CHECK(!parse("$GPGGA,120000,5021.6,N,00408.4,W,0,00,,,M,,M,,*hh", &m));

    /* ---- checksum failure is reported, not silently accepted ---- */
    CHECK(parse("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,"
                "46.9,M,,*00", &m));
    CHECK(m.checksum_present && !m.checksum_ok);

    /* ---- rubbish is rejected ---- */
    CHECK(!parse("hello world", &m));
    CHECK(!parse("", &m));
    CHECK(!parse("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48", &m));  /* not a fix */
    CHECK(!parse("*26.232/00:00:00.0 F:5.0 S:100", &m));   /* field out of range */

    /* ---- checksum helper on the canonical example ---- */
    CHECK(mv_nmea_checksum("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,"
                           "545.4,M,46.9,M,,*47", 0
        + strlen("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,"
                 "545.4,M,46.9,M,,*47")) == 0x47);

    /* ---- line assembler: CRLF, LF and CR all terminate ---- */
    MvLineAsm a;
    mv_lineasm_reset(&a);
    cap_n = 0;
    mv_lineasm_feed(&a, "abc\r\nde", 7, cap_cb, NULL);   /* one full, one partial */
    CHECK(cap_n == 1);
    CHECK_STR(cap_lines[0], "abc");
    mv_lineasm_feed(&a, "f\nghi\r", 6, cap_cb, NULL);    /* completes def, then ghi */
    CHECK(cap_n == 3);
    CHECK_STR(cap_lines[1], "def");
    CHECK_STR(cap_lines[2], "ghi");
)
