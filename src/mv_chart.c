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
#include "mv_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GUT_B 26.0f
#define GUT_T 22.0f
#define GUT_R 14.0f

#define HIT_R        14.0f
#define MIN_M_PER_PX (0.02)
#define MAX_M_PER_PX (4000.0)

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

static float gutter_left(struct nk_context *ctx, float scale)
{
    float w = text_w(ctx, "180\xc2\xb0 88.8888' W") + 12.0f * scale;
    float min = 40.0f * scale;
    return w < min ? min : w;
}

enum { OC_L = 1, OC_R = 2, OC_T = 4, OC_B = 8 };

static int outcode(struct nk_rect r, float x, float y)
{
    int c = 0;
    if (x < r.x)       c |= OC_L;
    if (x > r.x + r.w) c |= OC_R;
    if (y < r.y)       c |= OC_T;
    if (y > r.y + r.h) c |= OC_B;
    return c;
}

static struct nk_rect rect_clip(struct nk_rect a, struct nk_rect b)
{
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return nk_rect(x0, y0, x1 - x0, y1 - y0);
}

static struct nk_color fade(struct nk_color c, struct nk_color bg, float k)
{
    struct nk_color o;
    o.r = (nk_byte)(bg.r + (c.r - bg.r) * k);
    o.g = (nk_byte)(bg.g + (c.g - bg.g) * k);
    o.b = (nk_byte)(bg.b + (c.b - bg.b) * k);
    o.a = 255;
    return o;
}

static struct nk_color lerp(struct nk_color a, struct nk_color b, float k)
{
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    struct nk_color o;
    o.r = (nk_byte)(a.r + (b.r - a.r) * k);
    o.g = (nk_byte)(a.g + (b.g - a.g) * k);
    o.b = (nk_byte)(a.b + (b.b - a.b) * k);
    o.a = 255;
    return o;
}

/* ------------------------------------------------------------------ */
/* View                                                                */
/* ------------------------------------------------------------------ */

void mv_chart_request_fit(MvChartView *v) { v->fit_pending = true; }

void mv_chart_zoom(MvChartView *v, const MvChartInfo *info,
                   double factor, struct nk_vec2 at)
{
    if (!v->have_view || factor <= 0.0)
        return;

    double next = v->m_per_px / factor;
    if (next < MIN_M_PER_PX) next = MIN_M_PER_PX;
    if (next > MAX_M_PER_PX) next = MAX_M_PER_PX;
    if (next == v->m_per_px)
        return;

    struct nk_rect p = info ? info->plot : nk_rect(0, 0, 0, 0);
    if (p.w > 1.0f && p.h > 1.0f &&
        at.x >= p.x && at.x <= p.x + p.w && at.y >= p.y && at.y <= p.y + p.h) {
        double dx_px = at.x - (p.x + p.w / 2.0);
        double dy_px = at.y - (p.y + p.h / 2.0);
        double d_e = dx_px * (v->m_per_px - next);
        double d_n = -dy_px * (v->m_per_px - next);
        MvGeoProj pr;
        mv_geo_proj_init(&pr, v->lat, v->lon);
        mv_geo_inverse(&pr, d_e, d_n, &v->lat, &v->lon);
    }

    v->m_per_px = next;
    v->follow = false;
}

void mv_chart_pan(MvChartView *v, double dx_px, double dy_px)
{
    if (!v->have_view)
        return;
    MvGeoProj pr;
    mv_geo_proj_init(&pr, v->lat, v->lon);
    mv_geo_inverse(&pr, -dx_px * v->m_per_px, dy_px * v->m_per_px,
                   &v->lat, &v->lon);
    v->follow = false;
}

static void do_fit(MvChartView *v, struct nk_rect in,
                   const MvSample *track, int n_track,
                   bool live_valid, double live_lat, double live_lon)
{
    MvGeoBounds b;
    mv_geo_bounds_reset(&b);
    for (int i = 0; i < n_track; i++)
        if (track[i].has_fix)
            mv_geo_bounds_add(&b, track[i].lat, track[i].lon);
    if (live_valid)
        mv_geo_bounds_add(&b, live_lat, live_lon);
    if (b.n == 0)
        return;

    mv_geo_bounds_centre(&b, &v->lat, &v->lon);

    double w_m = 0, h_m = 0;
    mv_geo_bounds_extent_m(&b, &w_m, &h_m);

    double need_x = (in.w > 1.0f) ? w_m / (in.w * 0.82) : 0.0;
    double need_y = (in.h > 1.0f) ? h_m / (in.h * 0.82) : 0.0;
    double m_per_px = (need_x > need_y) ? need_x : need_y;

    if (!(m_per_px > 0.0) || !isfinite(m_per_px))
        m_per_px = (in.w > 1.0f) ? 500.0 / in.w : 0.5;
    if (m_per_px < MIN_M_PER_PX) m_per_px = MIN_M_PER_PX;
    if (m_per_px > MAX_M_PER_PX) m_per_px = MAX_M_PER_PX;

    v->m_per_px = m_per_px;
    v->have_view = true;
}

/* ------------------------------------------------------------------ */
/* Furniture                                                           */
/* ------------------------------------------------------------------ */

static void scale_label(char *dst, size_t cap, double metres)
{
    if (metres < 1000.0) snprintf(dst, cap, "%.0f m", metres);
    else                 snprintf(dst, cap, "%.4g km", metres / 1000.0);
}

static void draw_graticule(struct nk_context *ctx, struct nk_command_buffer *cb,
                           const MvTheme *t, float scale, struct nk_rect in,
                           const MvGeoProj *pr, double m_per_px)
{
    double lat_lo, lon_lo, lat_hi, lon_hi;
    mv_geo_inverse(pr, -in.w / 2.0 * m_per_px, -in.h / 2.0 * m_per_px,
                   &lat_lo, &lon_lo);
    mv_geo_inverse(pr,  in.w / 2.0 * m_per_px,  in.h / 2.0 * m_per_px,
                   &lat_hi, &lon_hi);

    int div_y = (int)(in.h / (110.0f * scale));
    int div_x = (int)(in.w / (150.0f * scale));
    if (div_y < 2) div_y = 2;
    if (div_x < 2) div_x = 2;

    double step_lat = mv_geo_grid_step_deg(lat_hi - lat_lo, div_y);
    double step_lon = mv_geo_grid_step_deg(lon_hi - lon_lo, div_x);

    char lab[48];
    float fh = ctx->style.font->height;

    double first = floor(lat_lo / step_lat) * step_lat;
    for (double lat = first; lat <= lat_hi + step_lat / 2; lat += step_lat) {
        double e, n;
        mv_geo_forward(pr, lat, pr->lon0, &e, &n);
        float y = in.y + in.h / 2.0f - (float)(n / m_per_px);
        if (y < in.y - 1 || y > in.y + in.h + 1)
            continue;
        nk_stroke_line(cb, in.x, y, in.x + in.w, y, 1.0f, t->plot_grid);
        mv_geo_format_lat(lab, sizeof lab, lat, step_lat);
        float w = text_w(ctx, lab);
        draw_text(ctx, cb, in.x - 8 * scale - w, y - fh / 2, w + 2, fh,
                  lab, t->text_dim);
    }

    float last_right = -1e9f;
    first = floor(lon_lo / step_lon) * step_lon;
    for (double lon = first; lon <= lon_hi + step_lon / 2; lon += step_lon) {
        double e, n;
        mv_geo_forward(pr, pr->lat0, lon, &e, &n);
        float x = in.x + in.w / 2.0f + (float)(e / m_per_px);
        if (x < in.x - 1 || x > in.x + in.w + 1)
            continue;
        nk_stroke_line(cb, x, in.y, x, in.y + in.h, 1.0f, t->plot_grid);
        mv_geo_format_lon(lab, sizeof lab, lon, step_lon);
        float w = text_w(ctx, lab);
        float lx = x - w / 2;
        if (lx < in.x) lx = in.x;
        if (lx + w > in.x + in.w) lx = in.x + in.w - w;
        if (lx < last_right + 10 * scale)
            continue;
        last_right = lx + w;
        draw_text(ctx, cb, lx, in.y + in.h + 6 * scale, w + 2, fh,
                  lab, t->text_dim);
    }
}

static void draw_scale_bar(struct nk_context *ctx, struct nk_command_buffer *cb,
                           const MvTheme *t, float scale, struct nk_rect in,
                           double m_per_px)
{
    double want_m = in.w * 0.25 * m_per_px;
    double bar_m  = mv_geo_nice_step(want_m);
    float  bar_px = (float)(bar_m / m_per_px);
    if (bar_px < 20 * scale || bar_px > in.w)
        return;

    float x = in.x + 12 * scale;
    float y = in.y + in.h - 16 * scale;
    float fh = ctx->style.font->height;

    nk_stroke_line(cb, x, y, x + bar_px, y, 2.0f * scale, t->plot_axis);
    nk_stroke_line(cb, x, y - 4 * scale, x, y + 4 * scale, 2.0f * scale, t->plot_axis);
    nk_stroke_line(cb, x + bar_px, y - 4 * scale, x + bar_px, y + 4 * scale,
                   2.0f * scale, t->plot_axis);

    char lab[32];
    scale_label(lab, sizeof lab, bar_m);
    draw_text(ctx, cb, x, y - fh - 6 * scale, bar_px + 40 * scale, fh,
              lab, t->text_dim);
}

static void draw_north(struct nk_context *ctx, struct nk_command_buffer *cb,
                       const MvTheme *t, float scale, struct nk_rect in)
{
    float x = in.x + in.w - 18 * scale;
    float y = in.y + 14 * scale;
    float h = 20 * scale;
    float fh = ctx->style.font->height;

    nk_stroke_line(cb, x, y + h, x, y, 1.5f * scale, t->plot_axis);
    nk_stroke_line(cb, x, y, x - 4 * scale, y + 6 * scale, 1.5f * scale, t->plot_axis);
    nk_stroke_line(cb, x, y, x + 4 * scale, y + 6 * scale, 1.5f * scale, t->plot_axis);
    draw_text(ctx, cb, x - text_w(ctx, "N") / 2, y + h + 1 * scale,
              20 * scale, fh, "N", t->text_dim);
}

/* Anomaly ramp: cool below the mean, warm above, near-neutral at the mean. */
static struct nk_color anomaly_colour(const MvTheme *t, double resid,
                                      double spread)
{
    if (!(spread > 0))
        return fade(t->trace_field, t->plot_bg, 0.85f);
    double k = resid / spread;               /* -1 .. +1 roughly */
    if (k < -1) k = -1;
    if (k > 1) k = 1;
    struct nk_color neutral = t->text_faint;
    if (k >= 0)
        return lerp(neutral, t->alarm, (float)k);     /* warm = high */
    return lerp(neutral, t->accent, (float)(-k));      /* cool = low  */
}

/* ------------------------------------------------------------------ */

void mv_chart_draw(struct nk_context *ctx, struct nk_rect area,
                   const MvTheme *t, float scale,
                   MvChartView *v,
                   const MvSample *track, int n_track,
                   bool live_valid, bool live_stale,
                   double live_lat, double live_lon,
                   struct nk_vec2 mouse, MvChartInfo *out)
{
    struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
    if (out) {
        memset(out, 0, sizeof *out);
        out->hit = -1;
    }
    if (!cb || area.w < 80 || area.h < 80)
        return;

    float gl = gutter_left(ctx, scale), gb = GUT_B * scale;
    float gt = GUT_T * scale, gr = GUT_R * scale;
    struct nk_rect in = nk_rect(area.x + gl, area.y + gt,
                                area.w - gl - gr, area.h - gt - gb);
    if (in.w < 40 || in.h < 40)
        return;

    nk_fill_rect(cb, area, 4.0f * scale, t->plot_bg);

    if (v->fit_pending || !v->have_view) {
        do_fit(v, in, track, n_track, live_valid, live_lat, live_lon);
        v->fit_pending = false;
    }
    if (v->follow && live_valid) {
        v->lat = live_lat;
        v->lon = live_lon;
    }

    float fh = ctx->style.font->height;
    if (!v->have_view) {
        const char *msg = "No positions yet — feed a GPS string through the "
                          "instrument, or over the same serial line.";
        draw_text(ctx, cb, in.x + 12 * scale, in.y + in.h / 2 - fh / 2,
                  in.w - 24 * scale, fh, msg, t->text_faint);
        if (out) out->plot = in;
        return;
    }

    MvGeoProj pr;
    mv_geo_proj_init(&pr, v->lat, v->lon);
    float cx = in.x + in.w / 2.0f;
    float cy = in.y + in.h / 2.0f;
    double mpp = v->m_per_px;

    draw_graticule(ctx, cb, t, scale, in, &pr, mpp);
    nk_stroke_rect(cb, in, 0.0f, 1.0f, t->plot_axis);

    /* Field statistics for the anomaly ramp: mean and a robust spread. */
    double mean = 0, spread = 0;
    int fn = 0;
    for (int i = 0; i < n_track; i++)
        if (track[i].has_fix && isfinite(track[i].field_nt)) {
            mean += track[i].field_nt;
            fn++;
        }
    if (fn > 0) {
        mean /= fn;
        for (int i = 0; i < n_track; i++)
            if (track[i].has_fix && isfinite(track[i].field_nt)) {
                double d = fabs(track[i].field_nt - mean);
                if (d > spread) spread = d;
            }
        if (spread < 1.0) spread = 1.0;
    }

    /* Data layers get their own scissor: coordinates run off the plot when
     * panned or following a moving vessel, and Nuklear's stroke commands are
     * not bounded by the rect they were computed from. */
    struct nk_rect clip_outer = cb->clip;
    struct nk_rect clip_plot = rect_clip(clip_outer, in);
    if (clip_plot.w <= 0.0f || clip_plot.h <= 0.0f)
        clip_plot = nk_rect(in.x, in.y, 0.0f, 0.0f);
    nk_push_scissor(cb, clip_plot);

    /* ---- track ----------------------------------------------------- */
    struct nk_color plain = fade(t->trace_field, t->plot_bg, 0.7f);
    float px = 0, py = 0;
    int prev_oc = 0;
    bool have_prev = false;
    int hit = -1;
    float hit_d2 = HIT_R * scale * HIT_R * scale;

    for (int i = 0; i < n_track; i++) {
        if (!track[i].has_fix)
            continue;
        double e, n;
        mv_geo_forward(&pr, track[i].lat, track[i].lon, &e, &n);
        float x = cx + (float)(e / mpp);
        float y = cy - (float)(n / mpp);
        int oc = outcode(in, x, y);

        if (have_prev && (prev_oc & oc) == 0) {
            struct nk_color col = plain;
            if (v->colour_field && isfinite(track[i].field_nt))
                col = anomaly_colour(t, track[i].field_nt - mean, spread);
            nk_stroke_line(cb, px, py, x, y, 2.0f * scale, col);
        }

        /* Cursor hit test against track vertices. */
        float dx = mouse.x - x, dy = mouse.y - y;
        if (dx * dx + dy * dy <= hit_d2) {
            hit_d2 = dx * dx + dy * dy;
            hit = i;
        }

        px = x; py = y; prev_oc = oc;
        have_prev = true;
    }

    /* ---- live position --------------------------------------------- */
    if (live_valid) {
        double e, n;
        mv_geo_forward(&pr, live_lat, live_lon, &e, &n);
        float x = cx + (float)(e / mpp);
        float y = cy - (float)(n / mpp);
        float r = 5.0f * scale;
        struct nk_color col = live_stale ? t->warn : t->ok;

        nk_stroke_line(cb, x - r * 2.2f, y, x + r * 2.2f, y, 1.0f * scale, col);
        nk_stroke_line(cb, x, y - r * 2.2f, x, y + r * 2.2f, 1.0f * scale, col);
        if (live_stale)
            nk_stroke_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2),
                             1.6f * scale, col);
        else
            nk_fill_circle(cb, nk_rect(x - r, y - r, r * 2, r * 2), col);
    }

    nk_push_scissor(cb, clip_outer);

    draw_scale_bar(ctx, cb, t, scale, in, mpp);
    draw_north(ctx, cb, t, scale, in);

    /* ---- cursor readout -------------------------------------------- */
    bool inside = mouse.x >= in.x && mouse.x <= in.x + in.w &&
                  mouse.y >= in.y && mouse.y <= in.y + in.h;
    double clat = 0, clon = 0;
    if (inside) {
        mv_geo_inverse(&pr, (mouse.x - cx) * mpp, (cy - mouse.y) * mpp,
                       &clat, &clon);
        char l1[64], l2[64], box[256];
        double res = mpp * 4.0;
        double step = res / 111320.0;
        mv_geo_format_lat(l1, sizeof l1, clat, step > 0 ? step : 1.0 / 3600);
        mv_geo_format_lon(l2, sizeof l2, clon, step > 0 ? step : 1.0 / 3600);

        if (hit >= 0 && isfinite(track[hit].field_nt))
            snprintf(box, sizeof box, "%s  %s    %.2f nT",
                     l1, l2, track[hit].field_nt);
        else
            snprintf(box, sizeof box, "%s  %s", l1, l2);

        float w = text_w(ctx, box) + 16 * scale;
        float h = fh + 8 * scale;
        float bx = mouse.x + 14 * scale;
        float by = mouse.y - h - 8 * scale;
        if (bx + w > in.x + in.w) bx = in.x + in.w - w;
        if (bx < in.x)            bx = in.x;
        if (by < in.y)            by = mouse.y + 12 * scale;

        struct nk_rect rr = nk_rect(bx, by, w, h);
        nk_fill_rect(cb, rr, 3.0f * scale, t->panel);
        nk_stroke_rect(cb, rr, 3.0f * scale, 1.0f, t->border);
        draw_text(ctx, cb, bx + 8 * scale, by + 4 * scale, w, fh, box, t->text);
    }

    if (out) {
        out->track_points = n_track;
        out->cursor_valid = inside;
        out->cursor_lat   = clat;
        out->cursor_lon   = clon;
        out->hit          = hit;
        out->plot         = in;
    }
}
