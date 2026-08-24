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
 * mv_help.h — the manual, as data
 *
 * One copy of the text, in a table, rendered two ways: the Help page draws it
 * in the program, and `magview --help-doc` writes it out as Markdown for
 * docs/HELP.md. A help file edited separately from the help page goes stale
 * the first time a control is renamed, so there is only the one.
 *
 * No SDL and no Nuklear here: this is content.
 */
#ifndef MV_HELP_H
#define MV_HELP_H

#include <stdio.h>

typedef enum {
    MV_HELP_TEXT = 0,   /* a paragraph                                    */
    MV_HELP_SUB,        /* a sub-heading within the section               */
    MV_HELP_BULLET,     /* one bullet                                     */
    MV_HELP_ROW,        /* control or key on the left, what it does right */
    MV_HELP_NOTE        /* something that will cost the operator data     */
} MvHelpKind;

typedef struct {
    MvHelpKind  kind;
    const char *a;      /* paragraph, bullet, heading, or the left column */
    const char *b;      /* MV_HELP_ROW only: the right column             */
} MvHelpItem;

typedef struct {
    const char       *title;
    const char       *intro;   /* may be NULL */
    const MvHelpItem *items;
    int               n_items;
} MvHelpSection;

/* The manual, in order. */
const MvHelpSection *mv_help_sections(int *n_sections);

/* Write the whole thing as Markdown — what `--help-doc` and the Makefile's
 * `help-doc` target use to regenerate docs/HELP.md. */
void mv_help_write_markdown(FILE *f, const char *version);

#endif /* MV_HELP_H */
