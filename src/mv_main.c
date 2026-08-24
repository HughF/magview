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
 * mv_main.c — entry point and frame loop
 *
 *   magview [--sim] [--port DEV] [--baud N]
 *
 * --sim runs against a synthetic Explorer that speaks the real wire format,
 * so the whole program can be exercised without an instrument.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mv_app.h"
#include "mv_ui.h"
#include "mv_help.h"
#include "mv_version.h"

#define BASE_W 1280
#define BASE_H  800

static void usage(void)
{
    printf("%s %s — %s\n\n", MAGVIEW_NAME, MAGVIEW_VERSION, MAGVIEW_TAGLINE);
    printf("  magview [options]\n\n"
           "  --sim            run against a simulated instrument\n"
           "  --port DEV       open this serial port at startup\n"
           "  --baud N         baud rate for --port (default 9600)\n"
           "  --help           this message\n"
           "  --help-doc       write the built-in manual to stdout as Markdown\n\n"
           "Environment:\n"
           "  MAGVIEW_SCALE    override the HiDPI UI scale (e.g. 2)\n");
}

int main(int argc, char **argv)
{
    bool simulate = false;
    const char *open_port = NULL;
    int baud = 9600;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sim") == 0) {
            simulate = true;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            open_port = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--help-doc") == 0) {
            /* Before SDL is touched, so docs/HELP.md can be regenerated in a
             * headless build. */
            mv_help_write_markdown(stdout, MAGVIEW_VERSION);
            return 0;
        } else {
            fprintf(stderr, "magview: unknown argument '%s'\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    SDL_Window *win = SDL_CreateWindow(
        MAGVIEW_NAME " " MAGVIEW_VERSION "  -  " MAGVIEW_TAGLINE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, BASE_W, BASE_H,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    MvApp *app = mv_app_create(simulate);
    if (!app) {
        fprintf(stderr, "magview: out of memory\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    MvUi *ui = mv_ui_create(win, ren, app);
    if (!ui) {
        fprintf(stderr, "magview: cannot create the interface\n");
        mv_app_destroy(app);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    mv_ui_fit_window(ui, BASE_W, BASE_H);
    SDL_ShowWindow(win);

    if (!simulate && open_port) {
        if (!mv_app_connect(app, open_port, baud))
            fprintf(stderr, "magview: %s\n", mv_app_state(app)->error);
    }

    while (!mv_ui_quit_requested(ui)) {
        SDL_Event e;
        mv_ui_input_begin(ui);
        while (SDL_PollEvent(&e))
            mv_ui_handle_event(ui, &e);
        mv_ui_input_end(ui);

        mv_app_poll(app);

        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        mv_ui_frame(ui, w, h);

        Uint8 r, g, b;
        mv_ui_clear_colour(ui, &r, &g, &b);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderClear(ren);
        mv_ui_render(ui);
        SDL_RenderPresent(ren);
    }

    mv_ui_destroy(ui);
    mv_app_destroy(app);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
