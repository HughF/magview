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
 * mv_proto.h — Marine Magnetics Explorer serial codec
 *
 * Pure functions over byte buffers. Nothing here opens a port.
 *
 * The Explorer (and its SeaSPY / SeaQuest siblings) can be set to several
 * output strings. Two are handled, because between them they cover every
 * telemetry format the family documents:
 *
 *   Tagged   *YY.JJJ/HH:MM:SS.S F:FFFFFF.FFF S:SSS D:+DDD.Dm A:AAA.A
 *            L:L TTTms_Q:QQ !!!! <CR><LF>
 *   Legacy   $ FFFFFF.FF  SSS  DDDD <CR><LF>     ($→'1' when field>=100000nT,
 *                                                 depth in units of 0.1 m)
 *
 * The parser is deliberately tolerant: it reads the tagged form by locating
 * each "X:" tag wherever it sits (the family concatenates "...ms_Q:QQ", so
 * fixed columns are unsafe), and the legacy form by tokenising the numbers
 * after the leading sign flag. We have no byte-faithful capture to pin exact
 * column widths against, so the tolerant reader plus the simulator in
 * mv_sim.c are what the tests exercise; see docs/PROTOCOL.md. A real capture
 * should be turned into a golden test the day one exists.
 *
 * GPS fixes ($GPGGA / $GPRMC / $GPGLL, any talker) are parsed too: the
 * Explorer can be configured to pass an aux GPS feed through the same wire,
 * and that is what gives every reading a position for the plotter.
 */
#ifndef MV_PROTO_H
#define MV_PROTO_H

#include "mv_types.h"
#include <stddef.h>

/* XOR of everything between '$'/'*' and '*'. Standard NMEA 0183. */
unsigned mv_nmea_checksum(const char *s, size_t len);

/*
 * Parse one line into *out. Returns true if the line was recognised as a mag
 * reading or a GPS fix. *out is zeroed first, so a false return never leaves
 * stale data behind. A trailing CR/LF is tolerated; a NUL is not required.
 *
 * A present-but-failing checksum is reported in out->checksum_ok rather than
 * rejected, and the line is still parsed — a dropped fix costs a position,
 * and every field is range-checked independently.
 */
bool mv_parse_line(const char *s, size_t len, MvMsg *out);

/* ------------------------------------------------------------------ */
/* Line assembler                                                      */
/*                                                                     */
/* Serial data arrives in arbitrary chunks. Feed the raw bytes in and it     */
/* calls `cb` once per complete line (CR, LF or CRLF terminated), with the   */
/* line NUL-terminated and its length. Over-long lines are flushed at the    */
/* buffer limit rather than growing without bound.                           */
/* ------------------------------------------------------------------ */

typedef struct {
    char   buf[512];
    size_t len;
} MvLineAsm;

void mv_lineasm_reset(MvLineAsm *a);
void mv_lineasm_feed(MvLineAsm *a, const void *data, size_t n,
                     void (*cb)(void *user, const char *line, size_t len),
                     void *user);

#endif /* MV_PROTO_H */
