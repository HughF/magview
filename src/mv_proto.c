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
#include "mv_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

unsigned mv_nmea_checksum(const char *s, size_t len)
{
    unsigned cs = 0;
    size_t i = 0;

    if (len && (s[0] == '$' || s[0] == '*' || s[0] == '!'))
        i = 1;
    for (; i < len && s[i] != '*'; i++)
        cs ^= (unsigned char)s[i];
    return cs & 0xFF;
}

/* Locate the two hex digits of an NMEA "*HH" checksum, if the line has one.
 * Returns true and fills *value / *body_len (bytes before the '*'). */
static bool find_checksum(const char *s, size_t len,
                          unsigned *value, size_t *body_len)
{
    /* Skip a leading sentinel: the tagged mag format opens with '*', which is
     * not a checksum delimiter — reading it as one flags every good line as a
     * bad checksum. The real NMEA checksum '*' is near the end. */
    size_t start = (len && (s[0] == '*' || s[0] == '$' || s[0] == '!')) ? 1 : 0;
    for (size_t i = start; i < len; i++) {
        if (s[i] == '*') {
            if (i + 2 < len &&
                isxdigit((unsigned char)s[i + 1]) &&
                isxdigit((unsigned char)s[i + 2])) {
                *value = (unsigned)strtol(s + i + 1, NULL, 16) & 0xFF;
                *body_len = i;
                return true;
            }
            /* A lone '*' with no two hex digits after it is not a checksum
             * (the tagged mag format ends in "!!!!", never "*HH"). */
        }
    }
    return false;
}

/*
 * Value of a tagged field: find "X:" and parse the number after it, skipping
 * a leading sign and stopping at the first character that cannot belong to a
 * decimal number. Returns MV_NA if the tag is absent or unparseable, so an
 * optional field the format omitted reads as "not available", never zero.
 *
 * Scans the whole line so it copes with the family's habit of gluing tags
 * together ("120ms_Q:07"): the "Q:" is found regardless of what precedes it.
 */
static double tag_value(const char *s, size_t len, char letter)
{
    for (size_t i = 0; i + 1 < len; i++) {
        if (s[i] == letter && s[i + 1] == ':') {
            const char *p = s + i + 2;
            const char *end = s + len;
            /* skip spaces between tag and value */
            while (p < end && *p == ' ')
                p++;
            char num[48];
            size_t w = 0;
            if (p < end && (*p == '+' || *p == '-') && w + 1 < sizeof num)
                num[w++] = *p++;
            bool seen_digit = false;
            while (p < end && w + 1 < sizeof num) {
                char c = *p;
                if (isdigit((unsigned char)c)) { seen_digit = true; num[w++] = c; }
                else if (c == '.')             { num[w++] = c; }
                else break;
                p++;
            }
            if (!seen_digit)
                return MV_NA;
            num[w] = '\0';
            return strtod(num, NULL);
        }
    }
    return MV_NA;
}

/* ------------------------------------------------------------------ */
/* GPS (NMEA position)                                                 */
/* ------------------------------------------------------------------ */

/* ddmm.mmmm[hemi] -> signed decimal degrees. NAN if the field is empty. */
static double nmea_coord(const char *field, char hemi)
{
    if (!field || !*field)
        return MV_NA;

    double v = strtod(field, NULL);
    double deg = floor(v / 100.0);
    double min = v - deg * 100.0;
    double dec = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W')
        dec = -dec;
    return dec;
}

/* Split NMEA body on commas into pointers within a scratch copy. */
static int nmea_split(char *scratch, char **f, int max)
{
    int n = 0;
    char *p = scratch;
    f[n++] = p;
    while (*p && n < max) {
        if (*p == ',') {
            *p = '\0';
            f[n++] = p + 1;
        }
        p++;
    }
    for (int i = n; i < max; i++)
        f[i] = scratch + strlen(scratch);  /* points at a NUL */
    return n;
}

static bool parse_gps(const char *s, size_t len, MvMsg *out)
{
    /* Copy the body (up to any '*') so we can NUL-split it. */
    unsigned cs_val = 0;
    size_t body = len;
    bool has_cs = find_checksum(s, len, &cs_val, &body);

    if (has_cs) {
        out->checksum_present = true;
        out->checksum_ok = (mv_nmea_checksum(s, len) == cs_val);
    }

    char buf[400];
    size_t n = body < sizeof buf - 1 ? body : sizeof buf - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';

    /* Strip a trailing CR/LF that fell inside the body when there was no
     * checksum to bound it. */
    while (n && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = '\0';

    char *f[20];
    int nf = nmea_split(buf, f, 20);
    if (nf < 1)
        return false;

    /* f[0] is like "$GPGGA"; the last three letters are the sentence type. */
    const char *tok = f[0];
    size_t tl = strlen(tok);
    if (tl < 4)
        return false;
    const char *type = tok + tl - 3;

    double lat = MV_NA, lon = MV_NA;
    bool valid = false;

    if (strncmp(type, "GGA", 3) == 0 && nf >= 6) {
        lat = nmea_coord(f[2], f[3][0]);
        lon = nmea_coord(f[4], f[5][0]);
        valid = (f[6][0] != '0' && f[6][0] != '\0');   /* fix quality > 0 */
    } else if (strncmp(type, "RMC", 3) == 0 && nf >= 7) {
        valid = (f[2][0] == 'A');
        lat = nmea_coord(f[3], f[4][0]);
        lon = nmea_coord(f[5], f[6][0]);
    } else if (strncmp(type, "GLL", 3) == 0 && nf >= 7) {
        lat = nmea_coord(f[1], f[2][0]);
        lon = nmea_coord(f[3], f[4][0]);
        valid = (f[6][0] == 'A');
    } else {
        return false;   /* recognised talker, but not a fix sentence */
    }

    if (!valid || !mv_has(lat) || !mv_has(lon))
        return false;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return false;

    out->kind = MV_MSG_GPS;
    out->lat = lat;
    out->lon = lon;
    return true;
}

/* ------------------------------------------------------------------ */
/* Magnetometer line                                                   */
/* ------------------------------------------------------------------ */

static bool field_in_range(double nt)
{
    /* Earth's field runs ~20,000–70,000 nT; allow a wide margin for
     * anomalies and for the instrument's full advertised range. A parse that
     * lands far outside this is a mis-read line, not a real anomaly. */
    return mv_has(nt) && nt > 1000.0 && nt < 200000.0;
}

/* Tagged form: has an "F:" (and usually "S:", "D:", "A:", "Q:"). */
static bool parse_tagged(const char *s, size_t len, MvMsg *out)
{
    double f = tag_value(s, len, 'F');
    if (!field_in_range(f))
        return false;

    out->kind    = MV_MSG_MAG;
    out->field_nt = f;
    out->signal   = tag_value(s, len, 'S');
    out->depth_m  = tag_value(s, len, 'D');
    out->altitude_m = tag_value(s, len, 'A');
    out->quality  = tag_value(s, len, 'Q');

    double leak = tag_value(s, len, 'L');
    if (mv_has(leak)) {
        out->has_leak = true;
        out->leak = (leak != 0.0);
    }

    /* Leading "*YY.JJJ/HH:MM:SS.S" — pull the wall time if it is there. */
    const char *slash = memchr(s, '/', len);
    if (slash) {
        int hh = 0, mm = 0; double ss = 0;
        if (sscanf(slash + 1, "%d:%d:%lf", &hh, &mm, &ss) == 3 &&
            hh >= 0 && hh < 24 && mm >= 0 && mm < 60 && ss >= 0 && ss < 60) {
            out->have_time = true;
            out->hour = hh;
            out->minute = mm;
            out->second = (int)ss;
            out->millis = (int)((ss - (int)ss) * 1000.0 + 0.5);
        }
    }

    unsigned cs_val = 0; size_t body;
    if (find_checksum(s, len, &cs_val, &body)) {
        out->checksum_present = true;
        out->checksum_ok = (mv_nmea_checksum(s, len) == cs_val);
    }
    return true;
}

/*
 * Legacy '$' form. After the '$' the family writes a single flag character:
 * a space when the field is below 100000 nT, or '1' when it is at or above —
 * that '1' is the hundred-thousands digit. Skipping only the space keeps the
 * '1', so the first whitespace-delimited number is the whole field either
 * way. Remaining numbers are signal, then depth in units of 0.1 m.
 */
static bool parse_legacy(const char *s, size_t len, MvMsg *out)
{
    /* Copy, trim, drop any checksum tail. */
    unsigned cs_val = 0; size_t body = len;
    bool has_cs = find_checksum(s, len, &cs_val, &body);

    char buf[128];
    size_t n = body < sizeof buf - 1 ? body : sizeof buf - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';

    const char *p = buf;
    if (*p == '$')
        p++;
    if (*p == ' ')           /* the "< 100000 nT" flag; a '1' is kept */
        p++;

    char *end = NULL;
    double f = strtod(p, &end);
    if (end == p || !field_in_range(f))
        return false;

    out->kind = MV_MSG_MAG;
    out->field_nt = f;

    /* signal */
    p = end;
    double sig = strtod(p, &end);
    out->signal = (end != p) ? sig : MV_NA;

    /* depth, reported in 0.1 m units */
    if (end != p) {
        p = end;
        double draw = strtod(p, &end);
        out->depth_m = (end != p) ? draw * 0.1 : MV_NA;
    } else {
        out->depth_m = MV_NA;
    }

    out->altitude_m = MV_NA;
    out->quality = MV_NA;

    if (has_cs) {
        out->checksum_present = true;
        out->checksum_ok = (mv_nmea_checksum(s, len) == cs_val);
    }
    return true;
}

/* ------------------------------------------------------------------ */

bool mv_parse_line(const char *s, size_t len, MvMsg *out)
{
    if (!out)
        return false;

    memset(out, 0, sizeof *out);
    out->depth_m = out->altitude_m = out->quality = MV_NA;
    out->signal = MV_NA;
    out->lat = out->lon = MV_NO_FIX;

    if (!s)
        return false;

    /* Skip leading whitespace. */
    while (len && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
        s++;
        len--;
    }
    /* Trim trailing whitespace/terminators. */
    while (len && (s[len - 1] == '\r' || s[len - 1] == '\n' ||
                   s[len - 1] == ' ' || s[len - 1] == '\t'))
        len--;
    if (len == 0)
        return false;

    /* NMEA GPS: '$' followed by a letter, e.g. "$GPGGA". The legacy mag form
     * is '$' followed by a space or a digit, so the two never collide. */
    if (s[0] == '$' && len > 1 && isalpha((unsigned char)s[1]))
        return parse_gps(s, len, out);

    /* Tagged mag: a '*' lead, or any line that actually carries an "F:" tag
     * with a value in field range. */
    if (s[0] == '*' || mv_has(tag_value(s, len, 'F')))
        if (parse_tagged(s, len, out))
            return true;

    /* Legacy '$' + flag. */
    if (s[0] == '$')
        return parse_legacy(s, len, out);

    /* Some units drop the '$' and emit bare "  51234.56  1024  152". Accept a
     * line that begins with a number in field range as legacy. */
    if (s[0] == ' ' || isdigit((unsigned char)s[0]) || s[0] == '+') {
        char probe[128];
        size_t n = len < sizeof probe - 1 ? len : sizeof probe - 1;
        memcpy(probe, s, n);
        probe[n] = '\0';
        double f = strtod(probe, NULL);
        if (field_in_range(f))
            return parse_legacy(s, len, out);
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Line assembler                                                      */
/* ------------------------------------------------------------------ */

void mv_lineasm_reset(MvLineAsm *a)
{
    if (a)
        a->len = 0;
}

void mv_lineasm_feed(MvLineAsm *a, const void *data, size_t n,
                     void (*cb)(void *user, const char *line, size_t len),
                     void *user)
{
    if (!a || !data)
        return;

    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c == '\r' || c == '\n') {
            if (a->len > 0) {
                a->buf[a->len] = '\0';
                if (cb)
                    cb(user, a->buf, a->len);
                a->len = 0;
            }
            continue;
        }
        if (a->len + 1 < sizeof a->buf) {
            a->buf[a->len++] = (char)c;
        } else {
            /* Over-long line: flush what we have rather than grow or wrap. */
            a->buf[a->len] = '\0';
            if (cb)
                cb(user, a->buf, a->len);
            a->len = 0;
            a->buf[a->len++] = (char)c;
        }
    }
}
