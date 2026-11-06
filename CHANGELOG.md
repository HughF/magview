# Changelog

All notable changes to magview. Dates are ISO-8601. Versioning is date-based
(`YYYY.MM.DD`), matching the rest of the toolchain.

## [Unreleased]

### Changed

- Checkboxes, radio buttons and drop-down arrows have the CM Acquire look.
  A checkbox is a rounded box with a thin outline, an accent outline on
  hover, and a solid accent fill with a drawn tick when ticked (the tick is
  dark on a light accent, white on a dark one). A radio button is a ring that
  turns accent when selected, with a small dot inside. A drop-down's arrow is
  a thin chevron instead of a solid triangle. The drawing is in
  `nk_draw_checkbox`, `nk_draw_option` and `nk_draw_symbol` in
  `third_party/nuklear.h`; the sizes and colours are set in the theme.

### Added

- CI (`.github/workflows/build.yml`): Linux build and tests, the Windows zip
  (built in an Arch container with the same mingw-w64/UCRT toolchain as the
  released `.exe`, import-checked, and started on a Windows runner) and a
  universal macOS `.app` in a `.dmg` (macOS 11+, SDL2 built from source, tests
  run natively, both slices started from the mounted image). Binaries are kept
  as workflow artifacts; nothing is published.
- `tools/mac/`: `build-sdl2.sh`, `build-universal.sh` and `make-app.sh`, which
  CI uses and which build the same `.dmg` on a Mac.

### Fixed

- The macOS build did not compile (found by the first CI run): the sources
  include `<SDL2/SDL.h>`, but `sdl2-config` only names `.../include/SDL2` and
  Apple's compiler does not search Homebrew's prefix, so every file including
  SDL failed. The Makefile now adds `<sdl2-config --prefix>/include` on macOS.
- `tools/win/get-sdl2.sh` checks the SDL2 mingw SDK against a pinned SHA-256
  (for the default version) instead of unpacking whatever it downloaded.

## [2026.08.25] — Windows build and first release

### Added
- **Windows binaries.** A mingw-w64 cross-build (`make windows` /
  `make windows-dist`) produces `magview.exe` with an icon, a version stamp
  and a per-monitor DPI-aware manifest, packaged with `SDL2.dll` and the docs
  into a zip. First tagged release: Windows zip + Linux AppImage + a
  `SHA256SUMS.txt` manifest, via `make release`.
- **Portable Linux AppImage** (`make appimage`): built against glibc 2.35 in a
  bubblewrap sandbox with a `dlopen`-backend SDL2, so it runs on any stable
  distribution with nothing to install.
- The interface now looks for a **bundled DejaVu Sans** beside the executable
  first (via `SDL_GetBasePath`), so text renders the same on a machine with no
  fonts installed — which is what the AppImage carries.
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
