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
#include "mv_config.h"

#include <string.h>

TEST_MAIN("test_config",
    MvSettings d;
    mv_settings_default(&d);
    CHECK(d.gps_source == MV_GPS_INTERLEAVED);
    CHECK(d.gps_baud == 4800);
    CHECK(d.gps_udp_port == 10110);

    /* Round-trip through the on-disk file. */
    MvSettings s;
    mv_settings_default(&s);
    s.gps_source = MV_GPS_UDP;
    s.gps_udp_port = 10110;
    snprintf(s.gps_port, sizeof s.gps_port, "/dev/ttyUSB7");
    s.gps_baud = 38400;
    CHECK(mv_settings_save(&s));

    MvSettings back;
    CHECK(mv_settings_load(&back));
    CHECK(back.gps_source == MV_GPS_UDP);
    CHECK(back.gps_udp_port == 10110);
    CHECK_STR(back.gps_port, "/dev/ttyUSB7");
    CHECK(back.gps_baud == 38400);

    /* Restore defaults on disk so a real run does not inherit the test's
     * settings. */
    mv_settings_default(&s);
    mv_settings_save(&s);
)
