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
 * mv_chart.h — plan view of the survey track
 *
 * A chart, not a map: a graticule, the vessel track, the live position, a
 * scale bar. No basemap imagery, so it needs no tile server, no internet on a
 * boat, and nothing beyond the SDL2 the rest of the program already uses.
 *
 * The track can be posted by field: each segment coloured by how far its
 * reading sits from the survey mean, cool below and warm above, so an anomaly
 * shows as a hot patch on the line the moment the boat passes over it.
 *
 * Drawn into the Nuklear command buffer like mv_plot, so it clips and
 * composites with the widgets around it.
 */
#ifndef MV_CHART_H
#define MV_CHART_H

#include "nk.h"
#include "mv_geo.h"
#include "mv_theme.h"
#include "mv_types.h"

typedef struct {
    double lat, lon;      /* what is at the centre of the plot          */
    double m_per_px;      /* zoom; larger is further out                */
    bool   have_view;
    bool   fit_pending;
    bool   follow;        /* keep the live position centred             */
    bool   colour_field;  /* post the track by field anomaly            */
} MvChartView;

typedef struct {
    int    track_points;
    bool   cursor_valid;
    double cursor_lat, cursor_lon;
    int    hit;           /* nearest track sample under cursor, or -1   */
    struct nk_rect plot;
} MvChartInfo;

void mv_chart_request_fit(MvChartView *v);
void mv_chart_zoom(MvChartView *v, const MvChartInfo *info,
                   double factor, struct nk_vec2 at);
void mv_chart_pan(MvChartView *v, double dx_px, double dy_px);

/*
 * `track` is the positioned readings, oldest first. `live_stale` marks a
 * position that is no longer being refreshed (drawn hollow rather than
 * filled). `out` may be NULL.
 */
void mv_chart_draw(struct nk_context *ctx, struct nk_rect area,
                   const MvTheme *t, float scale,
                   MvChartView *v,
                   const MvSample *track, int n_track,
                   bool live_valid, bool live_stale,
                   double live_lat, double live_lon,
                   struct nk_vec2 mouse, MvChartInfo *out);

#endif /* MV_CHART_H */
