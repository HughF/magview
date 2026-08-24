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
 * mv_ui.c — window shell, pages and dialogs
 *
 * Same layout rule as its sibling svpview: the shell computes an exact pixel
 * rect for every region from the window size, and each region is its own
 * Nuklear window at that rect. Nothing is positioned by flow, so no region
 * can push another out of place at any window size or UI scale.
 */
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nk.h"
#include "nuklear_sdl_renderer.h"

#include "mv_ui.h"
#include "mv_theme.h"
#include "mv_plot.h"
#include "mv_chart.h"
#include "mv_geo.h"
#include "mv_help.h"
#include "mv_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Unscaled metrics, multiplied by the HiDPI scale at use. */
#define STATUS_H   46.0f
#define ACTION_H   32.0f
#define RAIL_W    142.0f
#define ROW_H      26.0f
#define LABEL_W   170.0f
#define BTN_W      96.0f
#define BTN_H      32.0f
#define DLG_BOTTOM_MARGIN 12.0f

#define WIN_MIN_W 1080
#define WIN_MIN_H 640
#define WIN_MAX_FRAC 0.85f

typedef enum {
    PAGE_FIELD = 0, PAGE_CHART, PAGE_LOG, PAGE_CONSOLE, PAGE_HELP,
    PAGE_COUNT
} MvPage;

static const char *PAGE_NAME[PAGE_COUNT] = {
    "Field", "Chart", "Log", "Console", "Help"
};

/* One-line hint for each rail page, shown on hover. */
static const char *PAGE_HINT[PAGE_COUNT] = {
    "The scrolling total-field strip, with optional depth and signal",
    "Plan view of the survey track, optionally posted by field",
    "Write every reading to a georeferenced CSV",
    "Every line the instrument sent, verbatim",
    "The manual, and what every control does (F1)"
};

typedef enum { DLG_NONE = 0, DLG_CONNECT, DLG_GPS, DLG_ABOUT } MvDialog;
typedef enum { DLG_R_NONE, DLG_R_CANCEL, DLG_R_APPLY, DLG_R_OK } DlgResult;

static const int BAUDS[] = { 4800, 9600, 19200, 38400, 57600, 115200 };
#define N_BAUD ((int)(sizeof BAUDS / sizeof BAUDS[0]))

static const double WINDOWS[] = { 30, 60, 120, 300, 600 };
static const char  *WINDOW_NAME[] = { "30 s", "1 min", "2 min", "5 min", "10 min" };
#define N_WINDOW ((int)(sizeof WINDOWS / sizeof WINDOWS[0]))

struct MvUi {
    struct nk_context *ctx;
    SDL_Window        *win;
    SDL_Renderer      *ren;
    MvApp             *app;

    const MvTheme *theme;
    bool     dark;
    bool     theme_toggle;
    float    scale;
    MvPage   page;
    bool     quit;

    MvDialog dialog;
    char     dlg_error[160];
    char     dlg_note[160];

    /* connect dialog */
    PlatPortInfo ports[PLAT_MAX_PORTS];
    int      n_ports, sel_port, sel_baud;

    /* gps-source dialog (working copy, committed on OK) */
    int      gps_source;
    int      gps_sel_port;
    int      gps_baud_idx;
    char     gps_udp_str[16];

    /* field page */
    int      win_idx;
    bool     show_depth;
    bool     show_signal;

    /* chart page */
    MvChartView chart;
    MvChartInfo chart_info;
    float    chart_drag_px;

    /* log page */
    char     log_dir[512];
    bool     log_raw;

    /* console follow-tail */
    int      con_seen;

    /* tooltips. tip_text is this frame's candidate, tip_shown the one the
     * dwell timer is counting against. */
    char     tip_text[256];
    struct nk_rect tip_over;
    char     tip_shown[256];
    uint64_t tip_since_ms;

    /* dialog auto-sizing */
    MvDialog dlg_measured;
    float    dlg_natural_h;

    char     title[192];
    MvPage   last_page;
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static float S(const MvUi *ui, float v) { return v * ui->scale; }

/* ------------------------------------------------------------------ */
/* Tooltips                                                            */
/*                                                                     */
/* Every control that does something says what it does when the pointer */
/* rests on it. tip() is called immediately before the widget, the way  */
/* Nuklear's nk_widget_is_hovered() idiom works, because the bounds     */
/* tested are the ones the *next* widget will occupy.                  */
/*                                                                     */
/* Nuklear's own nk_tooltip() is not usable here: it draws into the     */
/* current window, so a hint on the narrow rail is cut off at its edge, */
/* and it only reports a hover for the focused window, which the rail   */
/* is not until it has been clicked. This draws into the overlay buffer */
/* instead, which is linked in last and belongs to no window.          */
/* ------------------------------------------------------------------ */

#define TIP_DELAY_MS  700       /* dwell before a hint appears           */
#define TIP_MAX_W     320.0f    /* unscaled; wider than this it wraps    */
#define TIP_MAX_LINES 4

typedef struct { int off, len; } MvTipLine;

static float tip_text_w(const MvUi *ui, const char *s, int len)
{
    const struct nk_user_font *f = ui->ctx->style.font;
    if (!f || len <= 0)
        return 0.0f;
    return f->width(f->userdata, f->height, s, len);
}

/* Greedy word wrap, the same rule Nuklear applies to a wrapped label. Returns
 * the line count and, if out is non-NULL, the first max_out line spans. */
static int wrap_text(const MvUi *ui, const char *s, float w,
                     MvTipLine *out, int max_out)
{
    float space = tip_text_w(ui, " ", 1);
    int lines = 0, start = 0, i = 0;
    float cur = 0.0f;

    while (s[i]) {
        int ws = i;
        while (s[i] && s[i] != ' ')
            i++;
        float word = tip_text_w(ui, s + ws, i - ws);

        if (cur > 0.0f && cur + space + word > w) {
            if (out && lines < max_out) {
                out[lines].off = start;
                out[lines].len = ws - 1 - start;   /* drop the space */
            }
            lines++;
            start = ws;
            cur = word;
        } else {
            cur += (cur > 0.0f ? space : 0.0f) + word;
        }
        while (s[i] == ' ')
            i++;
    }

    if (out && lines < max_out) {
        out[lines].off = start;
        out[lines].len = i - start;
    }
    return lines + 1;
}

/* Height a wrapped label needs for `text` in a column `w` wide. */
static float wrap_height(const MvUi *ui, const char *text, float w)
{
    int n = wrap_text(ui, text, w, NULL, 0);
    return (float)n * (ui->ctx->style.font->height + S(ui, 4));
}

/*
 * Register a hint for the widget about to be laid out. The last one
 * registered in a frame wins, so the innermost widget under the pointer is
 * the one that speaks.
 */
static void tip(MvUi *ui, const char *text)
{
    struct nk_context *c = ui->ctx;

    if (!text || !text[0] || !c->current || !c->current->layout)
        return;
    if (c->current->popup.active)
        return;                    /* a dropdown is covering what is under it */
    if (c->input.mouse.buttons[NK_BUTTON_LEFT].down)
        return;                    /* nothing pops up mid-click */

    struct nk_rect b = nk_widget_bounds(c);
    struct nk_rect clip = c->current->layout->clip;

    if (!nk_input_is_mouse_hovering_rect(&c->input, b) ||
        !nk_input_is_mouse_hovering_rect(&c->input, clip))
        return;

    snprintf(ui->tip_text, sizeof ui->tip_text, "%s", text);
    ui->tip_over = b;
}

/* Draw the hint the pointer has rested on, after every window is finished. It
 * goes in ctx->overlay, the buffer Nuklear links in last, so it can hang over
 * a panel edge and sit above a dialog. */
static void tip_draw(MvUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;

    if (!ui->tip_text[0]) {
        ui->tip_shown[0] = '\0';
        return;
    }

    /* Dwell against the text, not the pointer: sliding along a row of buttons
     * re-arms it, moving within one control does not. */
    if (strcmp(ui->tip_text, ui->tip_shown) != 0) {
        snprintf(ui->tip_shown, sizeof ui->tip_shown, "%s", ui->tip_text);
        ui->tip_since_ms = plat_now_ms();
        return;
    }
    if (plat_now_ms() - ui->tip_since_ms < TIP_DELAY_MS)
        return;

    MvTipLine ln[TIP_MAX_LINES];
    int n = wrap_text(ui, ui->tip_shown, S(ui, TIP_MAX_W), ln, TIP_MAX_LINES);
    if (n > TIP_MAX_LINES)
        n = TIP_MAX_LINES;

    float lh = c->style.font->height + S(ui, 3);
    float pad_x = S(ui, 9), pad_y = S(ui, 6);
    float tw = 0.0f;
    for (int i = 0; i < n; i++) {
        float lw = tip_text_w(ui, ui->tip_shown + ln[i].off, ln[i].len);
        if (lw > tw) tw = lw;
    }

    struct nk_rect r;
    r.w = tw + pad_x * 2.0f;
    r.h = (float)n * lh + pad_y * 2.0f;

    /* Below the control it belongs to, flipping above when there is no room,
     * anchored to the control rather than the pointer so it does not jitter. */
    r.x = ui->tip_over.x;
    r.y = ui->tip_over.y + ui->tip_over.h + S(ui, 6);
    if (r.y + r.h > (float)h - S(ui, 4))
        r.y = ui->tip_over.y - r.h - S(ui, 6);
    if (r.y < S(ui, 4))
        r.y = S(ui, 4);
    if (r.x + r.w > (float)w - S(ui, 4))
        r.x = (float)w - r.w - S(ui, 4);
    if (r.x < S(ui, 4))
        r.x = S(ui, 4);

    struct nk_command_buffer *cb = &c->overlay;
    nk_command_buffer_init(cb, &c->memory, NK_CLIPPING_ON);
    nk_start_buffer(c, cb);
    nk_push_scissor(cb, nk_rect(0, 0, (float)w, (float)h));

    float rad = S(ui, 3);
    nk_fill_rect(cb, nk_rect(r.x + S(ui, 2), r.y + S(ui, 2), r.w, r.h),
                 rad, nk_rgba(0, 0, 0, t->is_light ? 36 : 90));
    nk_fill_rect(cb, r, rad, t->panel_alt);
    nk_stroke_rect(cb, r, rad, 1.0f, t->border);

    for (int i = 0; i < n; i++) {
        struct nk_rect lr = nk_rect(r.x + pad_x, r.y + pad_y + (float)i * lh,
                                    tw, lh);
        nk_draw_text(cb, lr, ui->tip_shown + ln[i].off, ln[i].len,
                     c->style.font, t->panel_alt, t->text);
    }

    nk_finish_buffer(c, cb);
}

static void form_row(MvUi *ui, float h)
{
    nk_layout_row_template_begin(ui->ctx, S(ui, h));
    nk_layout_row_template_push_static(ui->ctx, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(ui->ctx);
    nk_layout_row_template_end(ui->ctx);
}

static void info_row(MvUi *ui, const char *k, const char *v)
{
    form_row(ui, ROW_H);
    nk_label_colored(ui->ctx, k, NK_TEXT_RIGHT, ui->theme->text_dim);
    nk_label(ui->ctx, v, NK_TEXT_LEFT);
}

static void info_rowf(MvUi *ui, const char *k, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    info_row(ui, k, buf);
}

static void section(MvUi *ui, const char *title)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, 22.0f), 1);
    nk_label_colored(ui->ctx, title, NK_TEXT_LEFT, ui->theme->accent);
}

static void gap(MvUi *ui, float h)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, h), 1);
    nk_spacing(ui->ctx, 1);
}

static float list_height(MvUi *ui, int rows, float row_h, float extra,
                         float min_h, float max_h)
{
    struct nk_context *c = ui->ctx;
    float pitch = S(ui, row_h) + c->style.window.spacing.y;
    float h = rows * pitch + S(ui, extra)
            + c->style.window.group_padding.y * 2.0f;
    if (h < S(ui, min_h)) h = S(ui, min_h);
    if (h > S(ui, max_h)) h = S(ui, max_h);
    return h;
}

static bool primary_button(MvUi *ui, const char *label)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.button.normal,
                             nk_style_item_color(t->accent));
    nk_style_push_style_item(c, &c->style.button.hover,
                             nk_style_item_color(t->accent_hover));
    nk_style_push_style_item(c, &c->style.button.active,
                             nk_style_item_color(t->accent_dim));
    nk_style_push_color(c, &c->style.button.text_normal, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.text_hover, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.border_color, t->accent);

    bool hit = nk_button_label(c, label) != 0;

    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    return hit;
}

/* Colour for the signal reading: the family's own quality bands. */
static struct nk_color signal_colour(const MvUi *ui, double sig)
{
    if (!isfinite(sig))      return ui->theme->text_faint;
    if (sig >= 1300.0)       return ui->theme->ok;
    if (sig >= 800.0)        return ui->theme->text;
    return ui->theme->warn;
}

static bool fix_is_stale(const MvState *st)
{
    return st->status.has_fix &&
           (plat_now_ms() - st->last_fix_ms) > MV_STALE_MS;
}

/* ------------------------------------------------------------------ */
/* Status strip                                                        */
/* ------------------------------------------------------------------ */

static const char *link_text(const MvState *st)
{
    switch (st->link) {
    case MV_LINK_CLOSED: return "Not connected";
    case MV_LINK_OPEN:   return st->status.have_data ? "Receiving" : "Open, no data";
    case MV_LINK_ERROR:  return "Link error";
    }
    return "?";
}

static struct nk_color link_colour(const MvUi *ui, const MvState *st)
{
    switch (st->link) {
    case MV_LINK_CLOSED: return ui->theme->text_faint;
    case MV_LINK_OPEN:   return st->status.have_data ? ui->theme->ok
                                                     : ui->theme->warn;
    case MV_LINK_ERROR:  return ui->theme->alarm;
    }
    return ui->theme->text;
}

static void draw_status(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 14), S(ui, 8)));

    if (nk_begin(c, "status", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 30));
        nk_layout_row_template_push_static(c, S(ui, 150));  /* instrument */
        nk_layout_row_template_push_static(c, S(ui, 130));  /* link       */
        nk_layout_row_template_push_static(c, S(ui, 190));  /* field      */
        nk_layout_row_template_push_static(c, S(ui, 110));  /* signal     */
        nk_layout_row_template_push_static(c, S(ui, 110));  /* depth      */
        nk_layout_row_template_push_dynamic(c);             /* fix        */
        nk_layout_row_template_push_static(c, S(ui,  90));  /* rate       */
        nk_layout_row_template_push_static(c, S(ui, 100));  /* log        */
        nk_layout_row_template_end(c);

        char buf[128];

        nk_label(c, st->simulate ? "Explorer (sim)" : "Explorer", NK_TEXT_LEFT);

        nk_label_colored(c, link_text(st), NK_TEXT_LEFT, link_colour(ui, st));

        if (st->status.have_data)
            snprintf(buf, sizeof buf, "%.2f nT", st->status.field_nt);
        else
            snprintf(buf, sizeof buf, "field —");
        nk_label_colored(c, buf, NK_TEXT_LEFT,
                         st->status.have_data ? t->trace_field : t->text_faint);

        if (isfinite(st->status.signal))
            snprintf(buf, sizeof buf, "sig %.0f", st->status.signal);
        else
            snprintf(buf, sizeof buf, "sig —");
        nk_label_colored(c, buf, NK_TEXT_LEFT, signal_colour(ui, st->status.signal));

        if (isfinite(st->status.depth_m))
            snprintf(buf, sizeof buf, "%.1f m", st->status.depth_m);
        else
            snprintf(buf, sizeof buf, "depth —");
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        if (st->status.has_fix) {
            snprintf(buf, sizeof buf, "%.5f  %.5f", st->status.lat, st->status.lon);
            nk_label_colored(c, buf, NK_TEXT_LEFT,
                             fix_is_stale(st) ? t->warn : t->text_dim);
        } else {
            nk_label_colored(c, "no GPS fix", NK_TEXT_LEFT, t->warn);
        }

        if (st->status.rate_hz > 0.05)
            snprintf(buf, sizeof buf, "%.1f Hz", st->status.rate_hz);
        else
            snprintf(buf, sizeof buf, "— Hz");
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        if (st->logging)
            nk_label_colored(c, "\xe2\x97\x8f REC", NK_TEXT_RIGHT, t->alarm);
        else
            nk_label_colored(c, "not logging", NK_TEXT_RIGHT, t->text_faint);
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Navigation rail                                                     */
/* ------------------------------------------------------------------ */

static void draw_rail(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 8), S(ui, 12)));

    if (nk_begin(c, "rail", r, NK_WINDOW_NO_SCROLLBAR)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            nk_layout_row_dynamic(c, S(ui, 32), 1);
            tip(ui, PAGE_HINT[i]);
            nk_bool on = (ui->page == (MvPage)i);
            if (nk_selectable_label(c, PAGE_NAME[i], NK_TEXT_LEFT, &on) && on)
                ui->page = (MvPage)i;
        }

        gap(ui, 10);

        if (!st->simulate) {
            nk_layout_row_dynamic(c, S(ui, 30), 1);
            if (st->link == MV_LINK_OPEN) {
                tip(ui, "Close the serial port. The instrument carries on "
                        "regardless");
                if (nk_button_label(c, "Disconnect"))
                    mv_app_disconnect(ui->app);
            } else {
                tip(ui, "Open the serial port the Explorer's cable is on");
                if (nk_button_label(c, "Connect...")) {
                    ui->dlg_error[0] = ui->dlg_note[0] = '\0';
                    ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
                    ui->sel_port = 0;
                    ui->dialog = DLG_CONNECT;
                }
            }
        }

        nk_layout_row_dynamic(c, S(ui, 30), 1);
        tip(ui, "Choose where the position fix comes from: the mag line, a "
                "separate serial port, or UDP");
        if (nk_button_label(c, "GPS source...")) {
            ui->dlg_error[0] = ui->dlg_note[0] = '\0';
            ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
            ui->gps_source = st->gps_source;
            ui->gps_sel_port = 0;
            for (int i = 0; i < ui->n_ports; i++)
                if (strcmp(ui->ports[i].path, st->gps_port) == 0)
                    ui->gps_sel_port = i;
            ui->gps_baud_idx = 0;
            for (int i = 0; i < N_BAUD; i++)
                if (BAUDS[i] == st->gps_baud)
                    ui->gps_baud_idx = i;
            snprintf(ui->gps_udp_str, sizeof ui->gps_udp_str, "%d",
                     st->gps_udp_port);
            ui->dialog = DLG_GPS;
        }

        nk_layout_row_dynamic(c, S(ui, 30), 1);
        tip(ui, "Forget the buffered readings and the track. Does not touch "
                "the log");
        if (nk_button_label(c, "Clear"))
            mv_app_clear(ui->app);

        gap(ui, 8);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        tip(ui, "Version, build and licence");
        if (nk_button_label(c, "About..."))
            ui->dialog = DLG_ABOUT;

        nk_layout_row_dynamic(c, S(ui, 30), 1);
        tip(ui, ui->dark ? "Switch to the light theme, for daylight on deck"
                         : "Switch to the dark theme, for a night bridge");
        if (nk_button_label(c, ui->dark ? "Light theme" : "Dark theme"))
            ui->theme_toggle = true;
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Action bar                                                          */
/* ------------------------------------------------------------------ */

static void draw_action(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding,
                       nk_vec2(S(ui, 14), S(ui, 6)));

    if (nk_begin(c, "action", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 20));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_push_static(c, S(ui, 320));
        nk_layout_row_template_end(c);

        char buf[220];
        int n = 0;
        mv_app_samples(ui->app, &n);

        if (st->error[0]) {
            nk_label_colored(c, st->error, NK_TEXT_LEFT, t->alarm);
        } else {
            int tn = 0;
            mv_app_track(ui->app, &tn);
            snprintf(buf, sizeof buf,
                     "%d reading%s buffered   \xc2\xb7   %d track point%s",
                     n, n == 1 ? "" : "s", tn, tn == 1 ? "" : "s");
            nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);
        }

        if (st->logging)
            snprintf(buf, sizeof buf, "logging %ld rows \xc2\xb7 %ld kB",
                     st->log_rows, st->log_bytes / 1024);
        else if (st->status.bad_lines || st->status.bad_checksum)
            snprintf(buf, sizeof buf, "%d unparsed \xc2\xb7 %d bad checksum",
                     st->status.bad_lines, st->status.bad_checksum);
        else
            snprintf(buf, sizeof buf, "%s", MAGVIEW_TAGLINE);
        nk_label_colored(c, buf, NK_TEXT_RIGHT,
                         st->logging ? t->ok : t->text_faint);
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Field page — the scrolling strips                                   */
/* ------------------------------------------------------------------ */

static void page_field(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    int n = 0;
    const MvSample *s = mv_app_samples(ui->app, &n);
    double window_s = WINDOWS[ui->win_idx];

    /* ---- control row ---- */
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 70));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);

    nk_label_colored(c, "Window", NK_TEXT_RIGHT, t->text_dim);
    tip(ui, "How many seconds of history the strips show at once");
    ui->win_idx = nk_combo(c, WINDOW_NAME, N_WINDOW, ui->win_idx,
                           (int)S(ui, 24),
                           nk_vec2(S(ui, 120), S(ui, 200)));

    nk_bool sd = ui->show_depth, ss = ui->show_signal;
    tip(ui, "Add a towfish-depth strip below the field, on the same time axis");
    nk_checkbox_label(c, "Depth strip", &sd);
    tip(ui, "Add a signal-strength strip, to watch the reading quality");
    nk_checkbox_label(c, "Signal strip", &ss);
    ui->show_depth = sd;
    ui->show_signal = ss;

    /* Live readout on the right of the control row. */
    char head[160];
    if (st->status.have_data) {
        snprintf(head, sizeof head, "%.2f nT", st->status.field_nt);
        nk_label_colored(c, head, NK_TEXT_RIGHT, t->trace_field);
    } else {
        nk_label_colored(c, "waiting for the instrument", NK_TEXT_RIGHT,
                         t->text_faint);
    }

    /* ---- work out how much height the strips share ---- */
    int nstrips = 1 + (ui->show_depth ? 1 : 0) + (ui->show_signal ? 1 : 0);

    /*
     * The strips are placed at explicit rects rather than threaded through
     * Nuklear's row layout. Stacking full-height plot rows and reading each
     * one's bounds accumulated a growing gap between them — the second and
     * third strips marched off the bottom of the window. Here one probe row
     * fixes the top edge, the space is claimed in a single spacer, and the
     * strips are drawn straight into the canvas at computed rects, which the
     * canvas clips to the panel anyway.
     */
    nk_layout_row_dynamic(c, 0.0f, 1);
    struct nk_rect anchor = nk_layout_widget_bounds(c);
    float top = anchor.y;
    float left = anchor.x;
    float width = anchor.w;

    float gapy = S(ui, 8);
    float avail = r.y + r.h - top - c->style.window.padding.y - S(ui, 2)
                - (nstrips - 1) * gapy;
    if (avail < S(ui, 120))
        avail = r.h * 0.65f;

    /* Field gets the lion's share; the companion strips are compact. */
    float unit = avail / (nstrips == 1 ? 1.0f
                        : nstrips == 2 ? 3.0f    /* 2 + 1 */
                                       : 4.0f);  /* 2 + 1 + 1 */
    float field_h = floorf((nstrips == 1) ? avail : unit * 2.0f);
    float aux_h   = floorf(unit);

    struct nk_vec2 mouse = (ui->dialog == DLG_NONE)
        ? nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y)
        : nk_vec2(-1, -1);

    float y = top;
    MvStripInfo fi;
    mv_plot_strip(c, nk_rect(left, y, width, field_h), t, ui->scale,
                  MV_STRIP_FIELD, s, n, window_s, mouse, &fi);
    y += field_h + gapy;

    if (ui->show_depth) {
        mv_plot_strip(c, nk_rect(left, y, width, aux_h), t, ui->scale,
                      MV_STRIP_DEPTH, s, n, window_s, mouse, NULL);
        y += aux_h + gapy;
    }
    if (ui->show_signal) {
        mv_plot_strip(c, nk_rect(left, y, width, aux_h), t, ui->scale,
                      MV_STRIP_SIGNAL, s, n, window_s, mouse, NULL);
        y += aux_h + gapy;
    }

    /* Claim the vertical space the strips occupy so the layout is consistent
     * and nothing else tries to draw over them. */
    nk_layout_row_dynamic(c, (y - gapy) - top, 1);
    nk_spacing(c, 1);
}

/* ------------------------------------------------------------------ */
/* Chart page — the plotter                                            */
/* ------------------------------------------------------------------ */

static void page_chart(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    int tn = 0;
    const MvSample *track = mv_app_track(ui->app, &tn);

    bool live = st->status.has_fix;
    bool live_stale = fix_is_stale(st);
    double live_lat = st->status.lat, live_lon = st->status.lon;

    /* ---- controls ---- */
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 56));
    nk_layout_row_template_push_static(c, S(ui, 40));
    nk_layout_row_template_push_static(c, S(ui, 40));
    nk_layout_row_template_push_static(c, S(ui, 150));
    nk_layout_row_template_push_static(c, S(ui, 130));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);

    tip(ui, "Set the view to contain the whole track");
    if (nk_button_label(c, "Fit"))
        mv_chart_request_fit(&ui->chart);

    struct nk_vec2 centre = nk_vec2(ui->chart_info.plot.x + ui->chart_info.plot.w / 2,
                                    ui->chart_info.plot.y + ui->chart_info.plot.h / 2);
    tip(ui, "Zoom in about the middle. The wheel zooms about the pointer");
    if (nk_button_label(c, "+"))
        mv_chart_zoom(&ui->chart, &ui->chart_info, 2.0, centre);
    tip(ui, "Zoom out about the middle");
    if (nk_button_label(c, "-"))
        mv_chart_zoom(&ui->chart, &ui->chart_info, 0.5, centre);

    nk_bool cf = ui->chart.colour_field;
    tip(ui, "Post each fix by its departure from the survey mean — cool low, "
            "warm high");
    nk_checkbox_label(c, "Colour by field", &cf);
    ui->chart.colour_field = cf;

    nk_bool follow = ui->chart.follow;
    tip(ui, "Keep the live position centred as the boat moves");
    nk_checkbox_label(c, "Follow", &follow);
    if (follow && !live)
        follow = nk_false;
    ui->chart.follow = follow;

    char across[64];
    if (ui->chart.have_view) {
        char d[48];
        mv_geo_format_distance(d, sizeof d,
                               ui->chart_info.plot.w * ui->chart.m_per_px);
        snprintf(across, sizeof across, "%s across", d);
    } else {
        snprintf(across, sizeof across, "no view");
    }
    nk_label_colored(c, across, NK_TEXT_RIGHT, t->text_dim);

    /* ---- plot + side panel ---- */
    nk_layout_row_dynamic(c, 0.0f, 1);
    float top = nk_layout_widget_bounds(c).y;
    float body_h = r.y + r.h - top
                 - c->style.window.spacing.y
                 - c->style.window.padding.y * 2.0f;
    if (body_h < S(ui, 120))
        body_h = r.h * 0.7f;

    nk_layout_row_template_begin(c, body_h);
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 230));
    nk_layout_row_template_end(c);

    struct nk_rect area = nk_widget_bounds(c);
    nk_spacing(c, 1);

    /* interaction before draw */
    struct nk_rect plot = ui->chart_info.plot;
    bool interactive = (ui->dialog == DLG_NONE) && plot.w > 1.0f;
    if (interactive) {
        if (nk_input_is_mouse_hovering_rect(&c->input, plot) &&
            c->input.mouse.scroll_delta.y != 0.0f) {
            double zf = pow(1.25, c->input.mouse.scroll_delta.y);
            mv_chart_zoom(&ui->chart, &ui->chart_info, zf,
                          nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y));
        }
        if (nk_input_has_mouse_click_down_in_rect(&c->input, NK_BUTTON_LEFT,
                                                  plot, nk_true)) {
            struct nk_vec2 d = c->input.mouse.delta;
            if (d.x != 0.0f || d.y != 0.0f)
                mv_chart_pan(&ui->chart, d.x, d.y);
        }
    }

    struct nk_vec2 m = (ui->dialog == DLG_NONE)
        ? nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y)
        : nk_vec2(-1, -1);

    mv_chart_draw(c, area, t, ui->scale, &ui->chart, track, tn,
                  live, live_stale, live_lat, live_lon, m, &ui->chart_info);

    /* ---- side panel ---- */
    if (nk_group_begin(c, "chartside", NK_WINDOW_BORDER)) {
        char buf[128], l1[48], l2[48];

        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, live_stale ? "Position (last known)" : "Position",
                         NK_TEXT_LEFT, t->accent);
        nk_layout_row_dynamic(c, S(ui, 22), 1);
        if (live) {
            mv_geo_format_lat(l1, sizeof l1, live_lat, 1.0 / 3600);
            mv_geo_format_lon(l2, sizeof l2, live_lon, 1.0 / 3600);
            nk_label(c, l1, NK_TEXT_LEFT);
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            nk_label(c, l2, NK_TEXT_LEFT);
        } else {
            nk_label_colored(c, "no fix", NK_TEXT_LEFT, t->warn);
        }

        gap(ui, 4);
        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, "Survey", NK_TEXT_LEFT, t->accent);

        nk_layout_row_dynamic(c, S(ui, 20), 1);
        snprintf(buf, sizeof buf, "%d track point%s", tn, tn == 1 ? "" : "s");
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        if (st->status.have_data) {
            nk_layout_row_dynamic(c, S(ui, 20), 1);
            snprintf(buf, sizeof buf, "field  %.1f nT", st->status.field_nt);
            nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);
        }

        /* cursor readout */
        if (ui->chart_info.cursor_valid) {
            gap(ui, 4);
            nk_layout_row_dynamic(c, S(ui, 20), 1);
            nk_label_colored(c, "Cursor", NK_TEXT_LEFT, t->accent);
            mv_geo_format_lat(l1, sizeof l1, ui->chart_info.cursor_lat, 1.0 / 3600);
            mv_geo_format_lon(l2, sizeof l2, ui->chart_info.cursor_lon, 1.0 / 3600);
            nk_layout_row_dynamic(c, S(ui, 20), 1);
            nk_label(c, l1, NK_TEXT_LEFT);
            nk_layout_row_dynamic(c, S(ui, 20), 1);
            nk_label(c, l2, NK_TEXT_LEFT);
        }

        gap(ui, 6);
        nk_layout_row_dynamic(c, S(ui, 40), 1);
        nk_label_colored_wrap(c,
            "Drag to pan, wheel to zoom. Colour-by-field posts each fix by its "
            "departure from the survey mean.", t->text_faint);

        nk_group_end(c);
    }
}

/* ------------------------------------------------------------------ */
/* Log page                                                            */
/* ------------------------------------------------------------------ */

static void page_log(MvUi *ui, struct nk_rect r)
{
    (void)r;
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    section(ui, "Survey log");

    nk_layout_row_dynamic(c, S(ui, 38), 1);
    nk_label_colored_wrap(c,
        "Every reading is written as one CSV row — field in nT, signal, depth, "
        "altitude, quality — each stamped with UTC and the latest GPS position, "
        "so the log is georeferenced mag data ready for a survey package.",
        t->text_dim);

    gap(ui, 4);

    form_row(ui, ROW_H);
    nk_label_colored(ui->ctx, "Folder", NK_TEXT_RIGHT, t->text_dim);
    tip(ui, "Where the CSV is written. Defaults to your Documents folder");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->log_dir,
                                   sizeof ui->log_dir, nk_filter_default);

    form_row(ui, ROW_H);
    nk_label_colored(ui->ctx, "", NK_TEXT_RIGHT, t->text_dim);
    nk_bool raw = ui->log_raw;
    tip(ui, "Also write every serial line verbatim to a .raw file");
    nk_checkbox_label(c, "Also keep the raw serial (.raw)", &raw);
    ui->log_raw = raw;

    gap(ui, 4);

    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_static(c, S(ui, 150));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_spacing(c, 1);
    if (!st->logging) {
        tip(ui, "Open a new CSV log and begin writing one row per reading");
        if (primary_button(ui, "Start logging")) {
            const char *err = mv_app_log_start(ui->app, ui->log_dir, ui->log_raw);
            if (err) {
                ui->dlg_note[0] = '\0';
                snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", err);
            } else {
                ui->dlg_error[0] = '\0';
            }
        }
    } else {
        tip(ui, "Close the log file");
        if (nk_button_label(c, "Stop logging"))
            mv_app_log_stop(ui->app);
    }
    if (ui->dlg_error[0]) {
        nk_label_colored(c, ui->dlg_error, NK_TEXT_LEFT, t->alarm);
    } else {
        nk_spacing(c, 1);
    }

    gap(ui, 6);
    section(ui, "Current file");

    if (st->logging) {
        info_row(ui, "File", st->log_path);
        info_rowf(ui, "Rows written", "%ld", st->log_rows);
        info_rowf(ui, "Size", "%.1f kB", st->log_bytes / 1024.0);
        info_row(ui, "Georeferenced",
                 st->status.has_fix ? "yes — GPS fix present"
                                    : "waiting for a GPS fix");
    } else {
        nk_layout_row_dynamic(c, S(ui, 22), 1);
        nk_label_colored(c, "Not logging.", NK_TEXT_LEFT, t->text_faint);
    }

    gap(ui, 6);
    section(ui, "Latest readings");

    int n = 0;
    const MvSample *s = mv_app_samples(ui->app, &n);

    nk_layout_row_dynamic(c, list_height(ui, 10, 22.0f, 0.0f, 120.0f, 340.0f), 1);
    if (nk_group_begin(c, "recent", NK_WINDOW_BORDER)) {
        nk_layout_row_template_begin(c, S(ui, 20));
        nk_layout_row_template_push_static(c, S(ui, 70));
        nk_layout_row_template_push_static(c, S(ui, 120));
        nk_layout_row_template_push_static(c, S(ui, 80));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_end(c);
        nk_label_colored(c, "t (s)", NK_TEXT_LEFT, t->text_faint);
        nk_label_colored(c, "field nT", NK_TEXT_LEFT, t->text_faint);
        nk_label_colored(c, "depth m", NK_TEXT_LEFT, t->text_faint);
        nk_label_colored(c, "position", NK_TEXT_LEFT, t->text_faint);

        int start = n > 12 ? n - 12 : 0;
        for (int i = n - 1; i >= start; i--) {
            char a[24], b[24], d[24], p[64];
            snprintf(a, sizeof a, "%.1f", s[i].tick);
            snprintf(b, sizeof b, "%.2f", s[i].field_nt);
            if (isfinite(s[i].depth_m)) snprintf(d, sizeof d, "%.2f", s[i].depth_m);
            else                        snprintf(d, sizeof d, "-");
            if (s[i].has_fix) snprintf(p, sizeof p, "%.5f, %.5f", s[i].lat, s[i].lon);
            else              snprintf(p, sizeof p, "no fix");

            nk_layout_row_template_begin(c, S(ui, 20));
            nk_layout_row_template_push_static(c, S(ui, 70));
            nk_layout_row_template_push_static(c, S(ui, 120));
            nk_layout_row_template_push_static(c, S(ui, 80));
            nk_layout_row_template_push_dynamic(c);
            nk_layout_row_template_end(c);
            nk_label(c, a, NK_TEXT_LEFT);
            nk_label_colored(c, b, NK_TEXT_LEFT, t->trace_field);
            nk_label(c, d, NK_TEXT_LEFT);
            nk_label_colored(c, p, NK_TEXT_LEFT,
                             s[i].has_fix ? t->text_dim : t->warn);
        }
        if (n == 0) {
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            nk_label_colored(c, "No readings yet.", NK_TEXT_LEFT, t->text_faint);
        }
        nk_group_end(c);
    }
}

/* ------------------------------------------------------------------ */
/* Console page                                                        */
/* ------------------------------------------------------------------ */

static void page_console(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 260));
    nk_layout_row_template_end(c);
    nk_label_colored(c, "Raw serial (every line the instrument sent)",
                     NK_TEXT_LEFT, t->text_dim);
    char buf[64];
    snprintf(buf, sizeof buf, "%d unparsed \xc2\xb7 %d bad cksum",
             st->status.bad_lines, st->status.bad_checksum);
    nk_label_colored(c, buf, NK_TEXT_RIGHT,
                     (st->status.bad_lines || st->status.bad_checksum)
                     ? t->warn : t->text_faint);

    nk_layout_row_dynamic(c, 0.0f, 1);
    float top = nk_layout_widget_bounds(c).y;
    float body_h = r.y + r.h - top
                 - c->style.window.spacing.y
                 - c->style.window.padding.y * 2.0f;
    if (body_h < S(ui, 120))
        body_h = r.h * 0.7f;

    int n = mv_app_console_count(ui->app);

    nk_layout_row_dynamic(c, body_h, 1);
    if (nk_group_begin(c, "console", NK_WINDOW_BORDER)) {
        for (int i = 0; i < n; i++) {
            nk_layout_row_dynamic(c, S(ui, 18), 1);
            nk_label(c, mv_app_console_line(ui->app, i), NK_TEXT_LEFT);
        }
        if (n == 0) {
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            nk_label_colored(c, "Nothing received yet.", NK_TEXT_LEFT,
                             t->text_faint);
        }

        /* Follow the tail when new lines arrive. */
        if (n != ui->con_seen) {
            ui->con_seen = n;
            nk_group_set_scroll(c, "console", 0, (nk_uint)(n * (int)S(ui, 18)));
        }
        nk_group_end(c);
    }
}

/* ------------------------------------------------------------------ */
/* Help page                                                           */
/* ------------------------------------------------------------------ */

static void page_help(MvUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    (void)r;

    struct nk_rect region = nk_window_get_content_region(c);
    float avail = region.w - c->style.text.padding.x * 2.0f - S(ui, 6);
    float col = S(ui, 720.0f);
    if (col > avail) col = avail;
    if (col < S(ui, 200)) col = S(ui, 200);

    float key_w = S(ui, 190.0f);
    float val_w = col - key_w - c->style.window.spacing.x;
    if (val_w < S(ui, 120)) {
        key_w = col * 0.35f;
        val_w = col - key_w - c->style.window.spacing.x;
    }

    float ind_w = S(ui, 18.0f);
    float bul_w = col - ind_w - c->style.window.spacing.x;

    /* One column `col` wide, and nothing after it — not a trailing dynamic
     * spacer, which nk_spacing turns into a second full row and riddles the
     * page with holes. */
    #define HELP_ROW1(h)  do {                                       \
        nk_layout_row_template_begin(c, (h));                        \
        nk_layout_row_template_push_static(c, col);                  \
        nk_layout_row_template_end(c);                               \
    } while (0)

    nk_layout_row_dynamic(c, S(ui, 30), 1);
    nk_label_colored(c, MAGVIEW_NAME "  " MAGVIEW_VERSION "  \xe2\x80\x94  manual",
                     NK_TEXT_LEFT, t->accent);

    HELP_ROW1(wrap_height(ui, "x", col));
    nk_label_colored(c,
        "F1 opens this page. Rest the pointer on any control for a one-line "
        "version of what it does.", NK_TEXT_LEFT, t->text_dim);

    int n_sec = 0;
    const MvHelpSection *sec = mv_help_sections(&n_sec);

    for (int i = 0; i < n_sec; i++) {
        gap(ui, 12);
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        nk_label_colored(c, sec[i].title, NK_TEXT_LEFT, t->accent);

        if (sec[i].intro) {
            HELP_ROW1(wrap_height(ui, sec[i].intro, col));
            nk_label_colored_wrap(c, sec[i].intro, t->text_dim);
        }

        for (int k = 0; k < sec[i].n_items; k++) {
            const MvHelpItem *e = &sec[i].items[k];

            switch (e->kind) {
            case MV_HELP_TEXT:
                HELP_ROW1(wrap_height(ui, e->a, col));
                nk_label_colored_wrap(c, e->a, t->text_dim);
                break;

            case MV_HELP_SUB:
                gap(ui, 4);
                nk_layout_row_dynamic(c, S(ui, 22), 1);
                nk_label_colored(c, e->a, NK_TEXT_LEFT, t->text);
                break;

            case MV_HELP_BULLET:
                nk_layout_row_template_begin(c, wrap_height(ui, e->a, bul_w));
                nk_layout_row_template_push_static(c, ind_w);
                nk_layout_row_template_push_static(c, bul_w);
                nk_layout_row_template_end(c);
                nk_label_colored(c, "\xc2\xb7",
                                 NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_TOP,
                                 t->text_faint);
                nk_label_colored_wrap(c, e->a, t->text_dim);
                break;

            case MV_HELP_ROW: {
                float hgt = wrap_height(ui, e->b ? e->b : "", val_w);
                nk_layout_row_template_begin(c, hgt);
                nk_layout_row_template_push_static(c, key_w);
                nk_layout_row_template_push_static(c, val_w);
                nk_layout_row_template_end(c);
                nk_label_colored(c, e->a,
                                 NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_TOP,
                                 t->text);
                nk_label_colored_wrap(c, e->b ? e->b : "", t->text_dim);
                break;
            }

            case MV_HELP_NOTE:
                HELP_ROW1(wrap_height(ui, e->a, col));
                nk_label_colored_wrap(c, e->a, t->warn);
                break;
            }
        }
    }

    gap(ui, 14);
    HELP_ROW1(wrap_height(ui, "x", col));
    nk_label_colored(c,
        "The same text is written to docs/HELP.md by: magview --help-doc",
        NK_TEXT_LEFT, t->text_faint);
    gap(ui, 10);

    #undef HELP_ROW1
}

/* ------------------------------------------------------------------ */
/* Dialogs                                                             */
/* ------------------------------------------------------------------ */

static DlgResult dialog_buttons(MvUi *ui, bool can_apply, const char *primary)
{
    struct nk_context *c = ui->ctx;
    DlgResult res = DLG_R_NONE;

    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_end(c);

    nk_spacing(c, 1);
    tip(ui, "Discard the changes and close. Escape does the same");
    if (nk_button_label(c, "Cancel"))
        res = DLG_R_CANCEL;
    if (!can_apply) nk_widget_disable_begin(c);
    tip(ui, can_apply ? "Commit and close" : "Nothing to commit yet");
    if (primary_button(ui, primary) && can_apply)
        res = DLG_R_OK;
    if (!can_apply) nk_widget_disable_end(c);
    return res;
}

static void dialog_message(MvUi *ui)
{
    if (ui->dlg_error[0]) {
        nk_layout_row_dynamic(ui->ctx, S(ui, 34), 1);
        nk_label_colored_wrap(ui->ctx, ui->dlg_error, ui->theme->alarm);
    } else if (ui->dlg_note[0]) {
        nk_layout_row_dynamic(ui->ctx, S(ui, 34), 1);
        nk_label_colored_wrap(ui->ctx, ui->dlg_note, ui->theme->ok);
    }
}

static void dlg_connect_body(MvUi *ui)
{
    struct nk_context *c = ui->ctx;

    nk_layout_row_dynamic(c, S(ui, 34), 1);
    nk_label_colored_wrap(c,
        "The Explorer's serial cable appears as a serial port. The default is "
        "9600 baud, 8N1.", ui->theme->text_dim);

    gap(ui, 4);
    section(ui, "Port");

    nk_layout_row_dynamic(c,
        list_height(ui, ui->n_ports, 26.0f, 0.0f, 74.0f, 260.0f), 1);
    if (nk_group_begin(c, "ports", NK_WINDOW_BORDER)) {
        for (int i = 0; i < ui->n_ports; i++) {
            nk_layout_row_dynamic(c, S(ui, 26), 1);
            nk_bool on = (i == ui->sel_port);
            char lab[300];
            snprintf(lab, sizeof lab, "%s   \xe2\x80\x94   %s",
                     ui->ports[i].path, ui->ports[i].label);
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                ui->sel_port = i;
        }
        if (ui->n_ports == 0) {
            nk_layout_row_dynamic(c, S(ui, 40), 1);
            nk_label_colored_wrap(c,
                "No serial ports found. Plug in the instrument cable, then "
                "press Rescan.", ui->theme->warn);
        }
        nk_group_end(c);
    }

    nk_layout_row_template_begin(c, S(ui, 30));
    nk_layout_row_template_push_static(c, S(ui, 110));
    nk_layout_row_template_push_static(c, S(ui, 120));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    tip(ui, "Look for serial ports again");
    if (nk_button_label(c, "Rescan")) {
        ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
        ui->sel_port = 0;
    }
    char baud_names[N_BAUD][12];
    const char *baud_ptr[N_BAUD];
    for (int i = 0; i < N_BAUD; i++) {
        snprintf(baud_names[i], sizeof baud_names[i], "%d", BAUDS[i]);
        baud_ptr[i] = baud_names[i];
    }
    tip(ui, "Serial baud rate. The Explorer default is 9600");
    ui->sel_baud = nk_combo(c, baud_ptr, N_BAUD, ui->sel_baud,
                            (int)S(ui, 24), nk_vec2(S(ui, 120), S(ui, 220)));
    nk_spacing(c, 1);
}

static bool dlg_connect_ready(MvUi *ui) { return ui->n_ports > 0; }

static void dlg_connect_commit(MvUi *ui, DlgResult r)
{
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
    } else if (r == DLG_R_OK) {
        if (ui->n_ports == 0) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "No port selected.");
            return;
        }
        bool ok = mv_app_connect(ui->app, ui->ports[ui->sel_port].path,
                                 BAUDS[ui->sel_baud]);
        if (!ok) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s",
                     mv_app_state(ui->app)->error);
        } else {
            ui->dlg_error[0] = '\0';
            ui->dialog = DLG_NONE;
        }
    }
}

/* ---- GPS source ---- */

static void dlg_gps_body(MvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;
    const MvState *st = mv_app_state(ui->app);

    nk_layout_row_dynamic(c, S(ui, 50), 1);
    nk_label_colored_wrap(c,
        "Every reading is tagged with the latest fix. Choose where that fix "
        "comes from — the mag's own output, a second serial port, or an NMEA "
        "feed over the network. The choice is remembered.", t->text_dim);

    gap(ui, 4);
    section(ui, "Source");

    nk_layout_row_dynamic(c, S(ui, 24), 1);
    tip(ui, "The Explorer passes NMEA GPS through its own serial output");
    if (nk_option_label(c, "Interleaved on the mag serial line",
                        ui->gps_source == MV_GPS_INTERLEAVED))
        ui->gps_source = MV_GPS_INTERLEAVED;
    nk_layout_row_dynamic(c, S(ui, 24), 1);
    tip(ui, "A second GPS device on its own serial port");
    if (nk_option_label(c, "A separate serial port",
                        ui->gps_source == MV_GPS_SERIAL))
        ui->gps_source = MV_GPS_SERIAL;
    nk_layout_row_dynamic(c, S(ui, 24), 1);
    tip(ui, "NMEA position over the network, e.g. from a navigation PC");
    if (nk_option_label(c, "UDP network (NMEA over IP)",
                        ui->gps_source == MV_GPS_UDP))
        ui->gps_source = MV_GPS_UDP;

    if (ui->gps_source == MV_GPS_SERIAL) {
        gap(ui, 4);
        section(ui, "GPS serial port");
        nk_layout_row_dynamic(c,
            list_height(ui, ui->n_ports, 26.0f, 0.0f, 60.0f, 200.0f), 1);
        if (nk_group_begin(c, "gpsports", NK_WINDOW_BORDER)) {
            for (int i = 0; i < ui->n_ports; i++) {
                nk_layout_row_dynamic(c, S(ui, 26), 1);
                nk_bool on = (i == ui->gps_sel_port);
                char lab[300];
                snprintf(lab, sizeof lab, "%s   \xe2\x80\x94   %s",
                         ui->ports[i].path, ui->ports[i].label);
                if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                    ui->gps_sel_port = i;
            }
            if (ui->n_ports == 0) {
                nk_layout_row_dynamic(c, S(ui, 34), 1);
                nk_label_colored_wrap(c, "No serial ports found.", t->warn);
            }
            nk_group_end(c);
        }
        nk_layout_row_template_begin(c, S(ui, 30));
        nk_layout_row_template_push_static(c, S(ui, 110));
        nk_layout_row_template_push_static(c, S(ui, 120));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_end(c);
        tip(ui, "Look for serial ports again");
        if (nk_button_label(c, "Rescan")) {
            ui->n_ports = plat_serial_list(ui->ports, PLAT_MAX_PORTS);
            ui->gps_sel_port = 0;
        }
        char baud_names[N_BAUD][12];
        const char *baud_ptr[N_BAUD];
        for (int i = 0; i < N_BAUD; i++) {
            snprintf(baud_names[i], sizeof baud_names[i], "%d", BAUDS[i]);
            baud_ptr[i] = baud_names[i];
        }
        tip(ui, "GPS baud rate. The classic NMEA GPS rate is 4800");
        ui->gps_baud_idx = nk_combo(c, baud_ptr, N_BAUD, ui->gps_baud_idx,
                                    (int)S(ui, 24), nk_vec2(S(ui, 120), S(ui, 220)));
        nk_spacing(c, 1);
    } else if (ui->gps_source == MV_GPS_UDP) {
        gap(ui, 4);
        section(ui, "UDP port");
        form_row(ui, ROW_H);
        nk_label_colored(c, "Listen on port", NK_TEXT_RIGHT, t->text_dim);
        tip(ui, "UDP port to receive NMEA on. Conventionally 10110");
        nk_edit_string_zero_terminated(c, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                                       ui->gps_udp_str, sizeof ui->gps_udp_str,
                                       nk_filter_decimal);
        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_label_colored(c, "NMEA over IP is conventionally port 10110.",
                         NK_TEXT_LEFT, t->text_faint);
    }

    gap(ui, 6);
    section(ui, "Current");
    if (st->gps_source == MV_GPS_INTERLEAVED)
        info_row(ui, "Source", "mag serial line");
    else if (st->gps_source == MV_GPS_SERIAL)
        info_rowf(ui, "Source", "serial %s @ %d %s", st->gps_port, st->gps_baud,
                  st->gps_link_open ? "(open)" : "(not open)");
    else
        info_rowf(ui, "Source", "UDP :%d %s", st->gps_udp_port,
                  st->gps_link_open ? "(listening)" : "(not open)");
    info_row(ui, "Fix", st->status.has_fix ? "present" : "none yet");
    if (st->gps_error[0]) {
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        nk_label_colored_wrap(c, st->gps_error, t->alarm);
    }
}

static void dlg_gps_commit(MvUi *ui, DlgResult r)
{
    if (r == DLG_R_CANCEL) {
        ui->dialog = DLG_NONE;
        return;
    }
    if (r != DLG_R_OK && r != DLG_R_APPLY)
        return;

    const char *port = (ui->n_ports > 0) ? ui->ports[ui->gps_sel_port].path : "";
    int udp = atoi(ui->gps_udp_str);
    const char *err = mv_app_set_gps(ui->app, ui->gps_source, port,
                                     BAUDS[ui->gps_baud_idx], udp);
    if (err) {
        ui->dlg_note[0] = '\0';
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", err);
    } else {
        ui->dlg_error[0] = '\0';
        if (r == DLG_R_OK)
            ui->dialog = DLG_NONE;
        else
            snprintf(ui->dlg_note, sizeof ui->dlg_note, "GPS source updated.");
    }
}

static bool dlg_gps_ready(MvUi *ui)
{
    if (ui->gps_source == MV_GPS_SERIAL)
        return ui->n_ports > 0;
    if (ui->gps_source == MV_GPS_UDP) {
        int p = atoi(ui->gps_udp_str);
        return p > 0 && p <= 65535;
    }
    return true;
}

static void dlg_about_body(MvUi *ui)
{
    struct nk_context *c = ui->ctx;
    const MvState *st = mv_app_state(ui->app);

    nk_layout_row_dynamic(c, S(ui, 26), 1);
    nk_label_colored(c, MAGVIEW_NAME "  " MAGVIEW_VERSION, NK_TEXT_LEFT,
                     ui->theme->accent);
    nk_layout_row_dynamic(c, S(ui, 20), 1);
    nk_label_colored(c, MAGVIEW_TAGLINE, NK_TEXT_LEFT, ui->theme->text);

    gap(ui, 6);
    nk_layout_row_dynamic(c, S(ui, 62), 1);
    nk_label_colored_wrap(c,
        "Reads a Marine Magnetics Explorer over its serial link, shows the "
        "total field on a scrolling strip, plots the survey track on a "
        "chart, and logs georeferenced readings to CSV. Cross-platform C and "
        "SDL2 — a straightforward replacement for the vendor's acquisition "
        "software.", ui->theme->text_dim);

    gap(ui, 6);
    section(ui, "This build");
    info_row(ui, "Version", MAGVIEW_VERSION);
    info_row(ui, "Built", __DATE__ " " __TIME__);
    SDL_version linked;
    SDL_GetVersion(&linked);
    info_rowf(ui, "SDL", "%d.%d.%d at runtime, built against %d.%d.%d",
              linked.major, linked.minor, linked.patch,
              SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    info_rowf(ui, "UI scale", "%.2f  (override with MAGVIEW_SCALE)",
              (double)ui->scale);

    gap(ui, 6);
    section(ui, "Link");
    info_row(ui, "Port", st->port[0] ? st->port : "not connected");
    info_rowf(ui, "Baud", "%d", st->baud);

    gap(ui, 6);
    section(ui, "Licence");
    info_row(ui, "Licence", "GPL-3.0-or-later");
    info_row(ui, "Copyright", "(C) 2026 Hugh Frater");
    nk_layout_row_dynamic(c, S(ui, 44), 1);
    nk_label_colored_wrap(c,
        "Free software with ABSOLUTELY NO WARRANTY; redistributable under the "
        "GNU General Public License v3 or later — see LICENSE. Nuklear, in "
        "third_party/, is MIT or public domain; SDL2 is zlib-licensed.",
        ui->theme->text_faint);
}

static const char *dialog_title(MvDialog d)
{
    switch (d) {
    case DLG_CONNECT: return "Connect to the Explorer";
    case DLG_GPS:     return "GPS source";
    case DLG_ABOUT:   return "About " MAGVIEW_NAME;
    default:          return "";
    }
}

static struct nk_vec2 dialog_size(MvDialog d)
{
    switch (d) {
    case DLG_CONNECT: return nk_vec2(560, 380);
    case DLG_GPS:     return nk_vec2(580, 520);
    case DLG_ABOUT:   return nk_vec2(600, 520);
    default:          return nk_vec2(500, 300);
    }
}

static void draw_dialog(MvUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;
    const MvTheme *t = ui->theme;

    /* The page behind is unreachable, so nothing on it may answer for the
     * pointer. Anything registered before now was drawn under the scrim. */
    ui->tip_text[0] = '\0';

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->scrim));
    if (nk_begin(c, "scrim", nk_rect(0, 0, (float)w, (float)h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {}
    nk_end(c);
    nk_style_pop_style_item(c);

    struct nk_vec2 sz = dialog_size(ui->dialog);
    float dw = S(ui, sz.x), dh = S(ui, sz.y);
    if (ui->dlg_measured == ui->dialog && ui->dlg_natural_h > 0.0f)
        dh = ui->dlg_natural_h;
    if (dw > w - S(ui, 40)) dw = w - S(ui, 40);
    if (dh > h - S(ui, 40)) dh = h - S(ui, 40);

    struct nk_rect r = nk_rect((w - dw) / 2, (h - dh) / 2, dw, dh);

    nk_style_push_float(c, &c->style.window.border, 1.0f);
    nk_style_push_color(c, &c->style.window.border_color, t->border);
    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->panel));

    nk_window_set_bounds(c, "dialog", r);

    if (nk_begin_titled(c, "dialog", dialog_title(ui->dialog), r,
                        NK_WINDOW_BORDER | NK_WINDOW_TITLE |
                        NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE |
                        NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_focus(c, "dialog");

        float spacing = c->style.window.spacing.y;
        float pad_y   = c->style.window.padding.y;
        float msg_h   = (ui->dlg_error[0] || ui->dlg_note[0])
                      ? S(ui, 38) + spacing : 0.0f;

        struct nk_rect region = nk_window_get_content_region(c);
        float body_h = region.h - S(ui, BTN_H) - spacing - msg_h - pad_y
                     - S(ui, DLG_BOTTOM_MARGIN);
        if (body_h < S(ui, 60))
            body_h = S(ui, 60);

        float chrome = nk_window_get_bounds(c).h - region.h;
        float used = 0.0f;

        nk_layout_row_dynamic(c, body_h, 1);
        if (nk_group_begin(c, "dlgbody", 0)) {
            struct nk_rect topb = nk_layout_widget_bounds(c);
            switch (ui->dialog) {
            case DLG_CONNECT: dlg_connect_body(ui); break;
            case DLG_GPS:     dlg_gps_body(ui);     break;
            case DLG_ABOUT:   dlg_about_body(ui);   break;
            default: break;
            }
            nk_layout_row_dynamic(c, 0.0f, 1);
            used = nk_layout_widget_bounds(c).y - topb.y - c->style.window.spacing.y;
            nk_group_end(c);
        }

        if (used > 0.0f) {
            float reserve = region.h - body_h;
            ui->dlg_natural_h = chrome + reserve + used
                              + c->style.window.group_padding.y * 2.0f;
            ui->dlg_measured = ui->dialog;
        }

        dialog_message(ui);

        DlgResult res;
        if (ui->dialog == DLG_ABOUT) {
            nk_layout_row_template_begin(c, S(ui, BTN_H));
            nk_layout_row_template_push_dynamic(c);
            nk_layout_row_template_push_static(c, S(ui, BTN_W));
            nk_layout_row_template_end(c);
            nk_spacing(c, 1);
            res = primary_button(ui, "OK") ? DLG_R_OK : DLG_R_NONE;
            if (res == DLG_R_OK)
                ui->dialog = DLG_NONE;
        } else if (ui->dialog == DLG_GPS) {
            res = dialog_buttons(ui, dlg_gps_ready(ui), "Save");
            if (res != DLG_R_NONE)
                dlg_gps_commit(ui, res);
        } else {
            res = dialog_buttons(ui, dlg_connect_ready(ui), "Connect");
            if (res != DLG_R_NONE)
                dlg_connect_commit(ui, res);
        }
    } else {
        ui->dialog = DLG_NONE;
    }
    nk_end(c);

    nk_style_pop_style_item(c);
    nk_style_pop_color(c);
    nk_style_pop_float(c);
}

/* ------------------------------------------------------------------ */
/* Shell                                                               */
/* ------------------------------------------------------------------ */

static void update_title(MvUi *ui)
{
    const MvState *st = mv_app_state(ui->app);
    char want[192];
    if (st->port[0])
        snprintf(want, sizeof want, "%s %s  -  Explorer on %.80s",
                 MAGVIEW_NAME, MAGVIEW_VERSION, st->port);
    else
        snprintf(want, sizeof want, "%s %s  -  %s",
                 MAGVIEW_NAME, MAGVIEW_VERSION, MAGVIEW_TAGLINE);
    if (strcmp(want, ui->title) != 0) {
        snprintf(ui->title, sizeof ui->title, "%s", want);
        SDL_SetWindowTitle(ui->win, ui->title);
    }
}

void mv_ui_frame(MvUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;

    if (ui->theme_toggle) {
        ui->theme_toggle = false;
        ui->dark = !ui->dark;
        ui->theme = ui->dark ? &MV_THEME_DARK : &MV_THEME_LIGHT;
        mv_theme_apply(c, ui->theme, ui->scale);
    }

    update_title(ui);

    /* This frame's hint is collected as the controls are laid out. */
    ui->tip_text[0] = '\0';

    float sh = S(ui, STATUS_H);
    float ah = S(ui, ACTION_H);
    float rw = S(ui, RAIL_W);
    float body_h = (float)h - sh - ah;
    if (body_h < 100.0f)
        body_h = 100.0f;

    draw_status(ui, nk_rect(0, 0, (float)w, sh));
    draw_rail(ui, nk_rect(0, sh, rw, body_h));
    draw_action(ui, nk_rect(0, (float)h - ah, (float)w, ah));

    struct nk_rect content = nk_rect(rw, sh, (float)w - rw, body_h);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(ui->theme->bg));
    bool scrolls = (ui->page == PAGE_LOG || ui->page == PAGE_HELP);
    if (nk_begin(c, "content", content,
                 scrolls ? 0 : NK_WINDOW_NO_SCROLLBAR)) {
        if (ui->page != ui->last_page) {
            nk_window_set_scroll(c, 0, 0);
            ui->last_page = ui->page;
        }
        struct nk_rect inner = nk_window_get_content_region(c);
        switch (ui->page) {
        case PAGE_FIELD:   page_field(ui, inner);   break;
        case PAGE_CHART:   page_chart(ui, inner);   break;
        case PAGE_LOG:     page_log(ui, inner);     break;
        case PAGE_CONSOLE: page_console(ui, inner); break;
        case PAGE_HELP:    page_help(ui, inner);    break;
        default: break;
        }
    }
    nk_end(c);
    nk_style_pop_style_item(c);

    if (ui->dialog != DLG_NONE)
        draw_dialog(ui, w, h);

    tip_draw(ui, w, h);
}

static float detect_scale(SDL_Window *win, SDL_Renderer *ren)
{
    const char *env = getenv("MAGVIEW_SCALE");
    if (env && atof(env) > 0.1)
        return (float)atof(env);

    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &dw, &dh);
    float scale = (ww > 0) ? (float)dw / (float)ww : 1.0f;

    float ddpi = 0;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, NULL, NULL) == 0) {
        float dpi_scale = ddpi / 96.0f;
        if (dpi_scale > scale)
            scale = dpi_scale;
    }
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

MvUi *mv_ui_create(SDL_Window *win, SDL_Renderer *ren, MvApp *app)
{
    MvUi *ui = calloc(1, sizeof *ui);
    if (!ui)
        return NULL;

    ui->win = win;
    ui->ren = ren;
    ui->app = app;
    ui->dark = true;
    ui->theme = &MV_THEME_DARK;
    ui->scale = detect_scale(win, ren);
    ui->page = PAGE_FIELD;
    ui->win_idx = 1;                 /* 1 min */
    ui->show_depth = true;
    ui->show_signal = false;
    ui->sel_baud = 1;                /* 9600 */
    ui->chart.colour_field = true;

    if (!plat_documents_dir(ui->log_dir, sizeof ui->log_dir))
        snprintf(ui->log_dir, sizeof ui->log_dir, ".");

    ui->ctx = nk_sdl_init(win, ren);
    if (!ui->ctx) {
        free(ui);
        return NULL;
    }

    struct nk_font_atlas *atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);

    static const nk_rune ranges[] = {
        0x0020, 0x00FF, 0x2010, 0x2027, 0x25A0, 0x25FF, 0
    };
    struct nk_font_config cfg = nk_font_config(14.0f * ui->scale);
    cfg.range = ranges;
    cfg.oversample_h = 2;
    cfg.oversample_v = 1;
    cfg.pixel_snap = 0;

    struct nk_font *font = NULL;
    static const char *candidates[] = {
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        NULL
    };
    for (int i = 0; candidates[i] && !font; i++)
        font = nk_font_atlas_add_from_file(atlas, candidates[i],
                                           14.0f * ui->scale, &cfg);
    nk_sdl_font_stash_end();
    if (font)
        nk_style_set_font(ui->ctx, &font->handle);

    mv_theme_apply(ui->ctx, ui->theme, ui->scale);
    return ui;
}

void mv_ui_fit_window(MvUi *ui, int base_w, int base_h)
{
    if (!ui)
        return;

    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(ui->win, &ww, &wh);
    SDL_GetRendererOutputSize(ui->ren, &dw, &dh);
    float px_per_unit = (ww > 0 && dw > 0) ? (float)dw / (float)ww : 1.0f;

    float unit_scale = ui->scale / px_per_unit;
    float w = (float)base_w * unit_scale;
    float h = (float)base_h * unit_scale;

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(ui->win),
                                   &usable) == 0 && usable.w > 0 && usable.h > 0) {
        float max_w = (float)usable.w * WIN_MAX_FRAC;
        float max_h = (float)usable.h * WIN_MAX_FRAC;
        float shrink = 1.0f;
        if (w > max_w) shrink = max_w / w;
        if (h > max_h && max_h / h < shrink) shrink = max_h / h;
        w *= shrink;
        h *= shrink;
    }

    int min_w = (int)((float)WIN_MIN_W * unit_scale);
    int min_h = (int)((float)WIN_MIN_H * unit_scale);
    SDL_SetWindowMinimumSize(ui->win, min_w, min_h);
    if (w < (float)min_w) w = (float)min_w;
    if (h < (float)min_h) h = (float)min_h;

    SDL_SetWindowSize(ui->win, (int)w, (int)h);
    SDL_SetWindowPosition(ui->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void mv_ui_destroy(MvUi *ui)
{
    if (!ui)
        return;
    nk_sdl_shutdown();
    free(ui);
}

void mv_ui_input_begin(MvUi *ui) { nk_input_begin(ui->ctx); }
void mv_ui_input_end(MvUi *ui)   { nk_input_end(ui->ctx); }

bool mv_ui_handle_event(MvUi *ui, SDL_Event *e)
{
    if (e->type == SDL_QUIT) {
        ui->quit = true;
        return true;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE &&
        ui->dialog != DLG_NONE) {
        ui->dialog = DLG_NONE;
        return true;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F1) {
        ui->dialog = DLG_NONE;
        ui->page = PAGE_HELP;
        return true;
    }
    return nk_sdl_handle_event(e) != 0;
}

void mv_ui_render(MvUi *ui) { nk_sdl_render(NK_ANTI_ALIASING_ON); }

void mv_ui_clear_colour(const MvUi *ui, Uint8 *r, Uint8 *g, Uint8 *b)
{
    *r = ui->theme->bg.r;
    *g = ui->theme->bg.g;
    *b = ui->theme->bg.b;
}

bool mv_ui_quit_requested(const MvUi *ui) { return ui->quit; }
