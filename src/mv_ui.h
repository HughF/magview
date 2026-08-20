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
#ifndef MV_UI_H
#define MV_UI_H

#include <SDL2/SDL.h>
#include "mv_app.h"

typedef struct MvUi MvUi;

MvUi *mv_ui_create(SDL_Window *win, SDL_Renderer *ren, MvApp *app);
void  mv_ui_destroy(MvUi *ui);

void  mv_ui_fit_window(MvUi *ui, int base_w, int base_h);

void  mv_ui_input_begin(MvUi *ui);
void  mv_ui_input_end(MvUi *ui);
bool  mv_ui_handle_event(MvUi *ui, SDL_Event *e);

void  mv_ui_frame(MvUi *ui, int win_w, int win_h);
void  mv_ui_render(MvUi *ui);

void  mv_ui_clear_colour(const MvUi *ui, Uint8 *r, Uint8 *g, Uint8 *b);
bool  mv_ui_quit_requested(const MvUi *ui);

#endif /* MV_UI_H */
