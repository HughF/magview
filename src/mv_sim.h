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
 * mv_sim.h — a synthetic Explorer, so the whole program runs with no hardware
 *
 * Models a boat steaming a lawnmower survey: a GPS fix once a second and a
 * mag reading at 4 Hz, in the real tagged wire format, with the field showing
 * dipole-shaped anomalies as the towfish passes buried features. The output
 * goes through exactly the same line assembler and parser the serial port
 * feeds, so --sim exercises the real pipeline end to end.
 */
#ifndef MV_SIM_H
#define MV_SIM_H

#include <stddef.h>
#include <stdint.h>

typedef struct MvSim MvSim;

MvSim *mv_sim_create(void);
void   mv_sim_destroy(MvSim *s);

/* Emit however many bytes the elapsed time is due, up to `cap`. Returns the
 * count. Call once per frame with the monotonic clock. */
int    mv_sim_read(MvSim *s, uint64_t now_ms, char *buf, size_t cap);

#endif /* MV_SIM_H */
