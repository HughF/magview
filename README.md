# magview

Acquisition and display software for the **Marine Magnetics Explorer** marine
magnetometer. Cross-platform C and SDL2, in the same house style as `svpview`
and `cm2view`.

It does the job the vendor's acquisition tool does, without being unpleasant
to use on a boat:

- **Scrolling field strip** — the total field on a chart-recorder style
  display that auto-ranges to what is on screen, so an anomaly of a few tens
  of nT riding on a ~50,000 nT background is actually visible instead of being
  a flat line on a 0–70,000 axis. Optional depth and signal-strength strips
  share the same time axis.
- **Chart plotter** — a plan view of the survey track with a proper graticule
  (degrees and decimal minutes), scale bar and north arrow, no internet or
  tile server needed. The track can be **posted by field**: each fix coloured
  by how far its reading sits from the survey mean, so a target shows as a hot
  patch on the line as the boat passes over it.
- **Logging** — every reading written to a CSV as it arrives, each row
  **georeferenced** with the latest GPS position and stamped with UTC, plus an
  optional verbatim `.raw` capture of the serial line.
- **Serial port picker** — enumerates the ports, pick one, choose the baud
  rate, connect. 9600 baud 8N1 is the Explorer's default.
- **GPS source** (rail → *GPS source…*) — choose where the position fix comes
  from and it is remembered between sessions:
  - *Interleaved* on the mag serial line (the Explorer's GPS pass-through),
  - a *separate serial port* (second GPS device, its own baud), or
  - *UDP* network NMEA (e.g. a nav PC broadcasting on port 10110).
- **Console** — every line the instrument sent, for when you need to see the
  wire.

## Building

Dependencies: **SDL2 only**. Nuklear is vendored in `third_party/`.

```
Debian/Ubuntu   sudo apt install libsdl2-dev
Arch            sudo pacman -S sdl2
macOS           brew install sdl2
```

```
make            # build ./magview
make run        # build and launch against the simulator
make test       # unit tests, sanitised (ASan + UBSan)
make windows    # cross-compile with mingw-w64
```

## Running

```
magview                     # start disconnected; pick a port from the UI
magview --sim               # run against a synthetic Explorer, no hardware
magview --port /dev/ttyUSB0 # open a port at startup
magview --port /dev/ttyUSB0 --baud 9600
```

`--sim` drives the whole program from a simulated instrument that speaks the
real wire format — a boat steaming a lawnmower survey, mag readings at 4 Hz
and a GPS fix every second, with buried anomalies to pass over. Everything
works without an instrument plugged in.

Environment: `MAGVIEW_SCALE` overrides the HiDPI UI scale (e.g. `2`).

## The log

One `magview_YYYYMMDD_HHMMSS.csv` per session, columns:

```
utc,tick_s,field_nT,signal,depth_m,altitude_m,quality,lat,lon
```

Each row is one reading: the magnetic data and the position it was taken at.
Optional fields (depth, altitude, quality) are left blank when the instrument's
output format did not carry them, rather than logged as a misleading zero. The
CSV is flushed after every row so a power interruption costs at most the line
in flight. With "keep the raw serial" ticked, a `.raw` file alongside it holds
every serial line verbatim as the unarguable original.

## Serial protocol

See [docs/PROTOCOL.md](docs/PROTOCOL.md). The parser reads the Explorer
family's tagged output (`*…F:…S:…D:…`) and the legacy `$`-prefixed form, and
merges any interleaved NMEA GPS (`$GPGGA`/`$GPRMC`/`$GPGLL`) to georeference
the readings.

## Licence

GPL-3.0-or-later. © 2026 Hugh Frater. See [LICENSE](LICENSE). Nuklear
(`third_party/`) is MIT or public domain; SDL2 is zlib-licensed.
