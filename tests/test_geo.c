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
#include "mv_geo.h"

TEST_MAIN("test_geo",
    CHECK(mv_geo_valid(50.0, -4.0));
    CHECK(!mv_geo_valid(MV_GEO_NO_FIX, MV_GEO_NO_FIX));

    /* One minute of latitude is ~1852 m. */
    double d = mv_geo_distance_m(50.0, -4.0, 50.0 + 1.0 / 60.0, -4.0);
    CHECK_NEAR(d, 1852.0, 5.0);

    /* North is bearing 0, east is 90. */
    CHECK_NEAR(mv_geo_bearing_deg(50.0, -4.0, 50.01, -4.0), 0.0, 1.0);
    CHECK_NEAR(mv_geo_bearing_deg(50.0, -4.0, 50.0, -3.99), 90.0, 1.0);

    /* Projection round-trips. */
    MvGeoProj p;
    mv_geo_proj_init(&p, 50.36, -4.14);
    double e, n, lat, lon;
    mv_geo_forward(&p, 50.37, -4.13, &e, &n);
    mv_geo_inverse(&p, e, n, &lat, &lon);
    CHECK_NEAR(lat, 50.37, 1e-6);
    CHECK_NEAR(lon, -4.13, 1e-6);

    /* Bounds centre and extent. */
    MvGeoBounds b;
    mv_geo_bounds_reset(&b);
    mv_geo_bounds_add(&b, 50.0, -4.0);
    mv_geo_bounds_add(&b, 50.02, -3.98);
    double clat, clon;
    CHECK(mv_geo_bounds_centre(&b, &clat, &clon));
    CHECK_NEAR(clat, 50.01, 1e-6);

    /* nice_step picks a 1/2/5 value. */
    CHECK_NEAR(mv_geo_nice_step(1234.0), 1000.0, 1e-6);
    CHECK_NEAR(mv_geo_nice_step(280.0), 200.0, 1e-6);
)
