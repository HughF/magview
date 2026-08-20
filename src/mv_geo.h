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
 * mv_geo.h — position maths for the chart page
 *
 * Pure functions, no I/O and no SDL, so all of this is unit-tested.
 *
 * Everything here works on a local tangent plane about a chosen origin,
 * because that is what the job is: casts within one survey area, tens of
 * kilometres apart at most. Over that extent a plane using the WGS84
 * metres-per-degree factors at the working latitude is accurate to a few
 * centimetres — better than a spherical formula on a mean radius, which is
 * out by roughly 0.3%. The distance and bearing functions fall back to the
 * spherical forms once the two positions are far enough apart for the plane
 * to stop being a good approximation, so a stray fix on the other side of the
 * world still gives a sane answer rather than nonsense.
 */
#ifndef MV_GEO_H
#define MV_GEO_H

#include <stdbool.h>
#include <stddef.h>

/* A latitude or longitude of 999 means "no fix" throughout the program. */
#define MV_GEO_NO_FIX 999.0

bool mv_geo_valid(double lat, double lon);

/* WGS84 metres per degree at a latitude. */
double mv_geo_m_per_deg_lat(double lat_deg);
double mv_geo_m_per_deg_lon(double lat_deg);

/* Distance (m) and initial bearing (degrees true, 0 <= b < 360). */
double mv_geo_distance_m(double lat1, double lon1, double lat2, double lon2);
double mv_geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);

/* ---- local tangent plane ------------------------------------------- */

typedef struct {
    double lat0, lon0;      /* origin                                   */
    double m_lat, m_lon;    /* metres per degree at the origin           */
} MvGeoProj;

void mv_geo_proj_init(MvGeoProj *p, double lat0, double lon0);

/* Position to metres east and north of the origin, and back. */
void mv_geo_forward(const MvGeoProj *p, double lat, double lon,
                    double *east_m, double *north_m);
void mv_geo_inverse(const MvGeoProj *p, double east_m, double north_m,
                    double *lat, double *lon);

/* ---- bounds -------------------------------------------------------- */

typedef struct {
    double min_lat, max_lat;
    double min_lon, max_lon;
    int    n;               /* positions added; 0 = empty              */
} MvGeoBounds;

void mv_geo_bounds_reset(MvGeoBounds *b);
void mv_geo_bounds_add(MvGeoBounds *b, double lat, double lon);

/* Centre of the bounds. False when nothing has been added. */
bool mv_geo_bounds_centre(const MvGeoBounds *b, double *lat, double *lon);

/* Extent in metres. Zero on both axes for a single position. */
void mv_geo_bounds_extent_m(const MvGeoBounds *b, double *w_m, double *h_m);

/* ---- graticule and scale ------------------------------------------- */

/*
 * Largest round number from the 1 / 2 / 5 sequence not greater than v.
 * Used for the scale bar, which has to be a number the eye can divide.
 */
double mv_geo_nice_step(double v);

/*
 * Graticule spacing in degrees for a span, chosen from the sexagesimal
 * sequence a chart uses — 30/10/5/2/1 degrees, then minutes, then seconds —
 * so gridlines land on whole minutes rather than on round metres.
 * Never returns zero.
 */
double mv_geo_grid_step_deg(double span_deg, int target_divisions);

/*
 * Degrees and decimal minutes, the form written on a survey log:
 * "50 25.584' N". Decimals follow `step_deg` so a label carries the
 * resolution the graticule is actually drawn at and no more.
 */
void mv_geo_format_lat(char *dst, size_t cap, double lat, double step_deg);
void mv_geo_format_lon(char *dst, size_t cap, double lon, double step_deg);

/* A distance for display: "864 m", or kilometres with the nautical miles a
 * mariner will ask for next. */
void mv_geo_format_distance(char *dst, size_t cap, double metres);

#endif /* MV_GEO_H */
