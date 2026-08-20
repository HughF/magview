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
 * mv_config.h — persisted settings
 *
 * A tiny key=value file under the per-user config directory. The only thing
 * worth remembering between sessions so far is where the GPS comes from — a
 * survey rig is wired one way and stays that way, so asking every launch would
 * be the wrong tool. Kept deliberately small; grow it a field at a time.
 */
#ifndef MV_CONFIG_H
#define MV_CONFIG_H

#include <stdbool.h>

typedef enum {
    MV_GPS_INTERLEAVED = 0,   /* NMEA rides the mag serial line          */
    MV_GPS_SERIAL      = 1,   /* a second serial port                    */
    MV_GPS_UDP         = 2     /* NMEA over the network (UDP)             */
} MvGpsSource;

typedef struct {
    int  gps_source;          /* MvGpsSource                             */
    char gps_port[256];       /* device path for MV_GPS_SERIAL           */
    int  gps_baud;            /* baud for MV_GPS_SERIAL                   */
    int  gps_udp_port;        /* listen port for MV_GPS_UDP              */
} MvSettings;

void mv_settings_default(MvSettings *s);

/* Fill *s with defaults, then overlay whatever the config file holds.
 * Returns true if a file was read; false (with defaults) otherwise. */
bool mv_settings_load(MvSettings *s);

/* Write *s to the config file. Returns false if it could not be written. */
bool mv_settings_save(const MvSettings *s);

#endif /* MV_CONFIG_H */
