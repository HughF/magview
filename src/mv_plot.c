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
#include "mv_plot.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Axis gutters, in unscaled points. */
#define GUT_L 66.0f
#define GUT_B 22.0f
#define GUT_T 22.0f
#define GUT_R 12.0f

typedef struct { float lo, hi; } Range;

static bool range_valid(Range r)
{
    return isfinite(r.lo) && isfinite(r.hi) && r.hi > r.lo;
}

/* Grow a range to a round number of divisions so gridlines land on values a
 * human would have chosen. */
static Range nice_range(double lo, double hi, int target_div)
{
    Range r = { 0, 1 };
    if (!(isfinite(lo) && isfinite(hi)) || hi < lo)
        return r;
    if (hi - lo < 1e-9) { hi += 0.5; lo -= 0.5; }

    double raw = (hi - lo) / (target_div > 0 ? target_div : 5);
    double mag = pow(10.0, floor(log10(raw)));
    double norm = raw / mag;
    double step = (norm <= 1.0) ? 1.0 : (norm <= 2.0) ? 2.0
                : (norm <= 5.0) ? 5.0 : 10.0;
    step *= mag;

    r.lo = (float)(floor(lo / step) * step);
    r.hi = (float)(ceil(hi / step) * step);
    if (!(r.hi > r.lo))
        r.hi = r.lo + (float)step;
    return r;
}

static int decimals_for(float span)
{
    if (span >= 1000.0f) return 0;
    if (span >= 100.0f)  return 0;
    if (span >= 10.0f)   return 1;
    if (span >= 1.0f)    return 2;
    return 3;
}

static void draw_text(struct nk_context *ctx, struct nk_command_buffer *cb,
                      float x, float y, float w, float h,
                      const char *s, struct nk_color col)
{
    nk_draw_text(cb, nk_rect(x, y, w, h), s, (int)strlen(s), ctx->style.font,
                 nk_rgba(0, 0, 0, 0), col);
}

static float text_w(struct nk_context *ctx, const char *s)
{
    const struct nk_user_font *f = ctx->style.font;
    return f->width(f->userdata, f->height, s, (int)strlen(s));
}

static double sample_value(const MvSample *s, MvStripKind kind)
{
    switch (kind) {
    case MV_STRIP_FIELD:  return s->field_nt;
    case MV_STRIP_DEPTH:  return s->depth_m;
    case MV_STRIP_SIGNAL: return s->signal;
    }
    return MV_NA;
}

static struct nk_color kind_colour(const MvTheme *t, MvStripKind kind)
{
    switch (kind) {
    case MV_STRIP_FIELD:  return t->trace_field;
    case MV_STRIP_DEPTH:  return t->trace_depth;
    case MV_STRIP_SIGNAL: return t->trace_signal;
    }
    return t->text;
}

static const char *kind_axis(MvStripKind kind)
{
    switch (kind) {
    case MV_STRIP_FIELD:  return "Field  nT";
    case MV_STRIP_DEPTH:  return "Depth  m";
    case MV_STRIP_SIGNAL: return "Signal";
    }
    return "";
}

void mv_plot_strip(struct nk_context *ctx, struct nk_rect area,
                   const MvTheme *t, float scale,
                   MvStripKind kind,
                   const MvSample *samples, int n, double window_s,
                   struct nk_vec2 mouse, MvStripInfo *out)
{
    if (out)
        memset(out, 0, sizeof *out);

    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
    if (!cb || area.w < 60 || area.h < 40)
        return;

    nk_fill_rect(cb, area, 4.0f * scale, t->plot_bg);

    float gl = GUT_L * scale, gb = GUT_B * scale;
    float gt = GUT_T * scale, gr = GUT_R * scale;
    struct nk_rect in = nk_rect(area.x + gl, area.y + gt,
                                area.w - gl - gr, area.h - gt - gb);
    if (in.w < 20 || in.h < 20)
        return;

    float fh = ctx->style.font->height;

    /* Newest tick anchors the right edge; the window extends back from it. */
    double now = -1e30;
    for (int i = n - 1; i >= 0; i--) {
        if (isfinite(samples[i].tick)) { now = samples[i].tick; break; }
    }
    if (now < -1e29 || window_s <= 0) {
        draw_text(ctx, cb, in.x + 10 * scale, in.y + in.h / 2 - fh / 2,
                  in.w, fh, "Waiting for data", t->text_faint);
        return;
    }
    double t0 = now - window_s;

    /* Visible value range. */
    double lo = 1e30, hi = -1e30;
    int vis = 0;
    for (int i = 0; i < n; i++) {
        if (samples[i].tick < t0)
            continue;
        double v = sample_value(&samples[i], kind);
        if (!isfinite(v))
            continue;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        vis++;
    }

    if (vis == 0) {
        draw_text(ctx, cb, in.x + 10 * scale, in.y + in.h / 2 - fh / 2,
                  in.w, fh, "No readings in the visible window", t->text_faint);
        nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);
        return;
    }

    /*
     * Auto-range with a little headroom. For the field this is the whole
     * point: the anomalies a survey is looking for are tens of nT riding on a
     * ~50,000 nT background, invisible on a fixed 0..70000 axis. Ranging to
     * what is on screen is what makes them show. A floor on the span stops a
     * dead-flat trace from being amplified into meaningless noise.
     */
    double span = hi - lo;
    double floor_span = (kind == MV_STRIP_FIELD) ? 4.0     /* nT   */
                      : (kind == MV_STRIP_DEPTH) ? 1.0     /* m    */
                                                 : 20.0;   /* raw  */
    if (span < floor_span) {
        double mid = (hi + lo) / 2.0;
        lo = mid - floor_span / 2.0;
        hi = mid + floor_span / 2.0;
    }
    Range r = nice_range(lo, hi, 5);
    if (!range_valid(r))
        return;

    bool inverted = (kind == MV_STRIP_DEPTH);  /* deeper is lower on screen */

    /* ---- Y grid + labels ------------------------------------------- */
    int dec = decimals_for(r.hi - r.lo);
    const int DIVS = 4;
    for (int i = 0; i <= DIVS; i++) {
        float fy = in.y + in.h * (float)i / DIVS;
        nk_stroke_line(cb, in.x, fy, in.x + in.w, fy, 1.0f, t->plot_grid);

        double v = inverted
            ? r.lo + (r.hi - r.lo) * (double)i / DIVS
            : r.hi - (r.hi - r.lo) * (double)i / DIVS;
        char lab[32];
        snprintf(lab, sizeof lab, "%.*f", dec, v);
        float w = text_w(ctx, lab);
        draw_text(ctx, cb, in.x - 8 * scale - w, fy - fh / 2,
                  w + 2, fh, lab, t->text_dim);
    }

    /* ---- X grid: time ticks every ~1/5 of the window --------------- */
    for (int i = 0; i <= 5; i++) {
        float fx = in.x + in.w * (float)i / 5;
        nk_stroke_line(cb, fx, in.y, fx, in.y + in.h, 1.0f, t->plot_grid);

        double secs_ago = window_s * (double)(5 - i) / 5;
        char lab[24];
        if (secs_ago < 0.5)      snprintf(lab, sizeof lab, "now");
        else if (secs_ago < 90)  snprintf(lab, sizeof lab, "-%.0fs", secs_ago);
        else                     snprintf(lab, sizeof lab, "-%.1fm", secs_ago / 60.0);
        float w = text_w(ctx, lab);
        float lx = fx - w / 2;
        if (lx < in.x) lx = in.x;
        if (lx + w > in.x + in.w) lx = in.x + in.w - w;
        draw_text(ctx, cb, lx, in.y + in.h + 5 * scale, w + 2, fh,
                  lab, t->text_faint);
    }

    nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);
    draw_text(ctx, cb, area.x + 4 * scale, area.y + 4 * scale, gl * 2, fh,
              kind_axis(kind), t->text_dim);

    /* ---- trace ----------------------------------------------------- */
    struct nk_color col = kind_colour(t, kind);
    double vspan = r.hi - r.lo;
    const float PIX_MIN = 0.6f * scale;

    float px = 0, py = 0;
    bool have_prev = false;
    for (int i = 0; i < n; i++) {
        double tk = samples[i].tick;
        double v = sample_value(&samples[i], kind);
        if (!isfinite(tk) || !isfinite(v))
            continue;
        if (tk < t0)
            continue;

        float x = in.x + in.w * (float)((tk - t0) / window_s);
        float frac = (float)((v - r.lo) / vspan);
        float y = inverted ? in.y + in.h * frac
                           : in.y + in.h * (1.0f - frac);

        if (have_prev) {
            bool last = (i == n - 1);
            if (!last && fabsf(x - px) < PIX_MIN && fabsf(y - py) < PIX_MIN)
                continue;
            nk_stroke_line(cb, px, py, x, y, 1.7f * scale, col);
        }
        px = x; py = y;
        have_prev = true;
    }

    /* Marker + guide line at the newest reading (right edge). */
    if (have_prev) {
        nk_fill_circle(cb, nk_rect(px - 3 * scale, py - 3 * scale,
                                   6 * scale, 6 * scale), col);
    }

    if (out) {
        out->have = true;
        out->vmin = lo;
        out->vmax = hi;
    }

    /* ---- cursor ---------------------------------------------------- */
    bool inside = mouse.x >= in.x && mouse.x <= in.x + in.w &&
                  mouse.y >= in.y && mouse.y <= in.y + in.h;
    if (inside) {
        nk_stroke_line(cb, mouse.x, in.y, mouse.x, in.y + in.h,
                       1.0f, t->plot_axis);
        double want_t = t0 + window_s * (double)((mouse.x - in.x) / in.w);

        int best = -1;
        double bd = 1e30;
        for (int i = 0; i < n; i++) {
            if (samples[i].tick < t0 || !isfinite(sample_value(&samples[i], kind)))
                continue;
            double diff = fabs(samples[i].tick - want_t);
            if (diff < bd) { bd = diff; best = i; }
        }
        if (best >= 0) {
            double v = sample_value(&samples[best], kind);
            char box[96];
            if (kind == MV_STRIP_FIELD)
                snprintf(box, sizeof box, "%.2f nT", v);
            else if (kind == MV_STRIP_DEPTH)
                snprintf(box, sizeof box, "%.2f m", v);
            else
                snprintf(box, sizeof box, "%.0f", v);

            float w = text_w(ctx, box) + 16 * scale;
            float h = fh + 8 * scale;
            float bx = mouse.x + 12 * scale;
            float by = in.y + 6 * scale;
            if (bx + w > in.x + in.w) bx = mouse.x - w - 12 * scale;

            struct nk_rect rr = nk_rect(bx, by, w, h);
            nk_fill_rect(cb, rr, 3.0f * scale, t->panel);
            nk_stroke_rect(cb, rr, 3.0f * scale, 1.0f, t->border);
            draw_text(ctx, cb, bx + 8 * scale, by + 4 * scale, w, fh, box,
                      t->text);

            if (out) {
                out->cursor = true;
                out->cursor_field = v;
                out->cursor_tick = samples[best].tick;
            }
        }
    }
}
