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
#include "mv_config.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_LEAF "settings.conf"

void mv_settings_default(MvSettings *s)
{
    if (!s)
        return;
    s->gps_source = MV_GPS_INTERLEAVED;
    s->gps_port[0] = '\0';
    s->gps_baud = 4800;                 /* the classic NMEA GPS baud */
    s->gps_udp_port = 10110;            /* IANA "NMEA-0183 over IP" port */
}

static bool config_path(char *buf, size_t cap)
{
    char dir[512];
    if (!plat_config_dir(dir, sizeof dir))
        return false;
    return plat_path_join(buf, cap, dir, CONFIG_LEAF);
}

/* Trim trailing CR/LF/space in place. */
static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

bool mv_settings_load(MvSettings *s)
{
    mv_settings_default(s);

    char path[600];
    if (!config_path(path, sizeof path))
        return false;

    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        rstrip(line);
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "gps_source") == 0) {
            int v = atoi(val);
            if (v >= MV_GPS_INTERLEAVED && v <= MV_GPS_UDP)
                s->gps_source = v;
        } else if (strcmp(key, "gps_port") == 0) {
            snprintf(s->gps_port, sizeof s->gps_port, "%s", val);
        } else if (strcmp(key, "gps_baud") == 0) {
            int v = atoi(val);
            if (v > 0)
                s->gps_baud = v;
        } else if (strcmp(key, "gps_udp_port") == 0) {
            int v = atoi(val);
            if (v > 0 && v <= 65535)
                s->gps_udp_port = v;
        }
    }
    fclose(f);
    return true;
}

bool mv_settings_save(const MvSettings *s)
{
    char path[600];
    if (!config_path(path, sizeof path))
        return false;

    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    fprintf(f, "gps_source=%d\n", s->gps_source);
    fprintf(f, "gps_port=%s\n", s->gps_port);
    fprintf(f, "gps_baud=%d\n", s->gps_baud);
    fprintf(f, "gps_udp_port=%d\n", s->gps_udp_port);
    fclose(f);
    return true;
}
