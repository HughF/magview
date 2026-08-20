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
 * mv_log.h — survey logging to disk
 *
 * Two files per session, in one directory:
 *   <prefix>_YYYYMMDD_HHMMSS.csv   one row per reading, the working record
 *   <prefix>_YYYYMMDD_HHMMSS.raw   every serial line verbatim (optional)
 *
 * The CSV is flushed after every row: a log that loses the last minute of a
 * line when the boat's power hiccups is exactly the kind of "shit software"
 * this program exists not to be. The raw file is the unarguable original if a
 * parsing question ever comes up.
 */
#ifndef MV_LOG_H
#define MV_LOG_H

#include "mv_types.h"
#include "plat.h"
#include <stddef.h>

typedef struct MvLog MvLog;

/*
 * Open a new log session. `dir` must exist. `prefix` names the files.
 * `raw` also opens the verbatim .raw file. On failure returns NULL and, if
 * `err` is non-NULL, points it at a static reason. The CSV path is copied
 * into `csv_path` (may be NULL).
 */
MvLog *mv_log_open(const char *dir, const char *prefix, bool raw,
                   char *csv_path, size_t csv_cap, const char **err);

void mv_log_close(MvLog *l);

/* One CSV row for a reading. `utc` timestamps it. */
void mv_log_row(MvLog *l, const PlatUtc *utc, const MvSample *s);

/* One verbatim serial line (no terminator needed). No-op if raw is off. */
void mv_log_raw(MvLog *l, const char *line);

long   mv_log_rows(const MvLog *l);
long   mv_log_bytes(const MvLog *l);

#endif /* MV_LOG_H */
