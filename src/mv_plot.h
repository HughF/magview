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
 * mv_plot.h — the scrolling magnetic field strip and its companion strips
 *
 * Drawn into the Nuklear command buffer rather than straight to the renderer,
 * so plots composite in the right order with the widgets around them and
 * inherit the same clipping.
 *
 * Time runs left to right with the newest reading pinned to the right edge,
 * the way a chart recorder lays paper down: the operator watches the right
 * edge and the trace scrolls away to the left.
 */
#ifndef MV_PLOT_H
#define MV_PLOT_H

#include "nk.h"
#include "mv_theme.h"
#include "mv_types.h"

typedef enum {
    MV_STRIP_FIELD = 0,    /* total field, auto-ranged in nT            */
    MV_STRIP_DEPTH,        /* towfish depth, deeper is down             */
    MV_STRIP_SIGNAL        /* signal strength                           */
} MvStripKind;

/* What the field strip measured this frame, for the readout beside it. */
typedef struct {
    bool   have;
    double vmin, vmax;     /* visible field range, nT                   */
    bool   cursor;         /* mouse was over the plot                   */
    double cursor_field;   /* field at the sample nearest the cursor    */
    double cursor_tick;
} MvStripInfo;

/*
 * Draw one strip. `samples` is oldest-first; `n` its length. `window_s` is
 * how many seconds are visible; the newest sample sits at the right edge.
 * Pass mouse in window coordinates for a cursor readout, or a zero-size point
 * for none. `out` may be NULL.
 */
void mv_plot_strip(struct nk_context *ctx, struct nk_rect area,
                   const MvTheme *t, float scale,
                   MvStripKind kind,
                   const MvSample *samples, int n, double window_s,
                   struct nk_vec2 mouse, MvStripInfo *out);

#endif /* MV_PLOT_H */
