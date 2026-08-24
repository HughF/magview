# Changelog

All notable changes to magview. Dates are ISO-8601. Versioning is date-based
(`YYYY.MM.DD`), matching the rest of the toolchain.

## [Unreleased]

### Added
- **Mouseover tooltips**: rest the pointer on any control for a one-line
  description. Drawn into the overlay buffer (not `nk_tooltip`) so a hint on
  the narrow rail is not clipped, with a short dwell before it appears.
- **Help page** (rail → *Help*, or **F1**): the manual, rendered from a single
  source (`src/mv_help.c`). `magview --help-doc` / `make help-doc` writes the
  same text to `docs/HELP.md`, so the in-app help and the file cannot drift.
- **GPS source** dialog (rail → *GPS source…*): the position fix can come from
  the mag serial line (interleaved, as before), a **separate serial port**, or
  **UDP** network NMEA (default port 10110). The choice is persisted to
  `settings.conf` under the per-user config directory and restored on launch.
- UDP listener back in the platform layer (POSIX + Win32) for the network GPS
  feed; `mv_config` settings module with a round-trip unit test.

## [2026.08.20] — first cut

Initial implementation.

### Added
- Scrolling magnetic **field strip** with auto-ranging so small anomalies on
  the large background field are visible; optional depth and signal strips on
  a shared time axis.
- **Chart plotter**: plan view of the survey track with a degrees/decimal-minute
  graticule, scale bar and north arrow, and an option to post the track by
  field anomaly (cool below the survey mean, warm above).
- **Logging** to CSV, one row per reading, each **georeferenced** with the
  latest GPS fix and stamped with UTC; optional verbatim `.raw` serial capture.
  The CSV is flushed after every row.
- **Serial** port picker with baud selection (default 9600 8N1); non-blocking,
  polled once per frame.
- **Console** page showing every serial line, with unparsed / bad-checksum
  counters.
- Explorer-family protocol codec: the tagged `*…F:…S:…D:…` string and the
  legacy `$`+flag form, plus interleaved NMEA GPS (`$GPGGA`/`$GPRMC`/`$GPGLL`).
- `--sim` simulated instrument driving the whole pipeline over the real wire
  format; runs with no hardware.
- Cross-platform: Linux/macOS (`plat_posix.c`) and Windows (`plat_win32.c`,
  `make windows`).
- Dark and light themes; HiDPI aware.
- Unit tests for the protocol, geo maths and log writer (ASan + UBSan).
