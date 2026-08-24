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
 * mv_help.c — the manual
 *
 * Written for someone on a survey boat, so it says what to do rather than how
 * the program is built. The tooltips give the one-line version of the same
 * thing; this is where the reasons live.
 */
#include "mv_help.h"

#include <stdbool.h>

#define T(s)      { MV_HELP_TEXT,   (s),  NULL }
#define SUB(s)    { MV_HELP_SUB,    (s),  NULL }
#define B(s)      { MV_HELP_BULLET, (s),  NULL }
#define R(k, v)   { MV_HELP_ROW,    (k),  (v)  }
#define N(s)      { MV_HELP_NOTE,   (s),  NULL }

/* ------------------------------------------------------------------ */

static const MvHelpItem it_about[] = {
    T("magview reads a Marine Magnetics Explorer over its serial link, shows "
      "the total field on a scrolling strip, plots the survey track on a "
      "chart, and logs every reading to a georeferenced CSV. It is a "
      "straightforward replacement for the vendor's acquisition software."),
    T("Everything works offline. There is no basemap imagery, no tile server "
      "and no internet dependency anywhere, because a survey vessel usually "
      "has none of them."),
    N("This is acquisition and display only. It does not configure the "
      "instrument's internal settings or command the tow; it reads what the "
      "Explorer sends and records it."),
};

static const MvHelpItem it_connect[] = {
    T("The Explorer's serial cable appears to the computer as a serial port. "
      "Press Connect... on the rail, choose the port, set the baud rate "
      "(9600, 8N1 is the Explorer's default) and connect. If the port you "
      "want is not listed, plug the cable in and press Rescan."),
    R("Not connected", "No port is open. Nothing is being read."),
    R("Open, no data",  "The port is open but no mag line has arrived yet. "
                        "Check the baud rate and that the instrument is "
                        "powered and sampling."),
    R("Receiving",      "Mag data is arriving and being parsed."),
    R("Link error",     "The port was lost — unplugged, or another program "
                        "took it. Reconnect."),
    T("Disconnect closes the port; the instrument carries on regardless. "
      "Clear empties the buffered readings and the track without touching "
      "the log."),
};

static const MvHelpItem it_status[] = {
    T("The strip across the top is the same on every page. Read it left to "
      "right."),
    R("Explorer",  "The instrument, or \"Explorer (sim)\" when running "
                   "against the simulator."),
    R("Link state","See Connecting."),
    R("Field",     "The latest total field, in nanoTesla."),
    R("Signal",    "Signal strength. It turns amber below 800 (weak) and "
                   "green above 1300 (excellent)."),
    R("Depth",     "Towfish depth, when the instrument's output carries it."),
    R("Position",  "Latitude and longitude of the latest fix, or \"no GPS "
                   "fix\". Amber when the fix has gone stale."),
    R("Rate",      "Measured sample rate, in hertz."),
    R("Logging",   "A red REC marker while a log is being written."),
};

static const MvHelpItem it_gps[] = {
    T("Every reading is tagged with the latest GPS fix, which is what "
      "georeferences the log and draws the plotter track. Rail, GPS "
      "source... chooses where that fix comes from, and the choice is "
      "remembered between sessions."),
    R("Interleaved on the mag line",
      "The Explorer is set to pass NMEA GPS through its own serial output, "
      "mixed in with the mag data. Nothing else to configure."),
    R("A separate serial port",
      "The GPS is its own device on a second port. Choose the port and its "
      "baud rate — 4800 is the classic NMEA GPS baud."),
    R("UDP network (NMEA over IP)",
      "The position comes over the network as NMEA, e.g. from a navigation "
      "PC. Set the port to listen on; 10110 is the conventional one."),
    T("A fix seen on the mag line is always used, whichever source is "
      "selected — if a rig sends it there, it counts. The dedicated sources "
      "are for rigs that do not."),
};

static const MvHelpItem it_field[] = {
    T("Field is the main acquisition display: the total field on a "
      "chart-recorder strip, newest at the right, scrolling away to the "
      "left."),
    R("Window",       "How many seconds of history are shown at once."),
    R("Depth strip",  "Add a towfish-depth strip below the field, on the "
                      "same time axis, drawn with deeper lower down."),
    R("Signal strip", "Add a signal-strength strip, to watch the quality of "
                      "the readings."),
    N("The field strip auto-ranges to what is on screen. This is the whole "
      "point: the anomalies a survey looks for are a few tens of nT riding "
      "on a background of around 50,000 nT, and a fixed 0 to 70,000 axis "
      "would hide them completely. The axis figures on the left tell you the "
      "range in force."),
    T("Rest the pointer anywhere on a strip for the value at that moment."),
};

static const MvHelpItem it_chart[] = {
    T("Chart is a plan view of the survey track and where the towfish is "
      "now. There is deliberately no basemap: a graticule, a scale bar and a "
      "north arrow work at sea with no data connection."),
    R("Fit",             "Set the view to contain the whole track."),
    R("+ and -",         "Zoom about the middle of the plot."),
    R("Colour by field", "Post each fix by how far its reading sits from the "
                         "survey mean — cool below, warm above — so a target "
                         "shows as a hot patch on the line as you pass over "
                         "it."),
    R("Follow",          "Keep the live position centred as the boat moves."),
    SUB("With the mouse"),
    R("Drag",  "Pan."),
    R("Wheel", "Zoom about the pointer."),
    T("The side panel gives the current position, the number of track "
      "points, the live field, and — when the pointer is over the plot — the "
      "position and field under it."),
    N("The track is drawn only from readings that carried a position. With "
      "no GPS fix there is no track, however much mag data is arriving; set "
      "the GPS source first."),
};

static const MvHelpItem it_log[] = {
    T("Log writes every reading to a CSV as it arrives. This is the survey "
      "record."),
    R("Folder",             "Where the file is written. It defaults to your "
                            "Documents folder."),
    R("Also keep the raw serial",
      "Alongside the CSV, write every serial line verbatim to a .raw file — "
      "the unarguable original if a question ever comes up."),
    R("Start / Stop logging", "Open or close the log file."),
    SUB("What a row holds"),
    T("Columns: utc, tick_s, field_nT, signal, depth_m, altitude_m, quality, "
      "lat, lon. One row per reading — the magnetic data and the position it "
      "was taken at, so the file is georeferenced mag data ready for a survey "
      "package."),
    N("A field the instrument's output did not carry is left blank rather "
      "than logged as a misleading zero. A reading taken before any GPS fix "
      "has an empty position; set the GPS source so every row is placed."),
    T("The file is flushed after every row, so a power interruption costs at "
      "most the line in flight. The Latest readings table shows the tail of "
      "what is being written."),
};

static const MvHelpItem it_console[] = {
    T("Console shows every line the instrument sent, verbatim, following the "
      "tail. It is the first place to look when something behaves oddly, and "
      "the first thing to quote in a fault report."),
    R("Unparsed",    "Lines that matched no known format. A steadily rising "
                     "count means the wrong baud rate, or an output format "
                     "magview does not yet read."),
    R("Bad checksum","Lines whose NMEA checksum did not verify. A handful is "
                     "normal; a climbing count points at a noisy link."),
    T("A line that fails its checksum is still parsed — losing a fix costs a "
      "position, and every field is range-checked anyway."),
};

static const MvHelpItem it_keys[] = {
    R("Hover",       "Rest the pointer on any control for a one-line "
                     "description of what it does."),
    R("F1",          "Open this page."),
    R("Escape",      "Cancel the dialog that is open."),
    R("Mouse wheel", "Zoom the chart, or scroll a list."),
    R("Drag",        "Pan the chart."),
    T("A dialog's title-bar close button means Cancel, the same as Escape."),
};

static const MvHelpItem it_trouble[] = {
    R("No serial ports listed",
      "Plug the cable in and press Rescan. On Linux the user must be in the "
      "dialout group to open a serial port."),
    R("Open, but no data",
      "Wrong baud rate, or the instrument is not sampling. 9600 8N1 is the "
      "Explorer default; try the others from the Connect dialog."),
    R("Unparsed count climbing",
      "The output format is one magview does not read, or the baud is wrong. "
      "Look at the Console to see what is actually arriving."),
    R("No track on the chart",
      "The readings have no position. Set the GPS source, and check the "
      "status strip shows a fix."),
    R("Field trace looks flat",
      "It is auto-ranged; read the axis figures. A genuinely flat trace over "
      "a small range is still drawn across the strip."),
    R("Text too small or too large",
      "The interface scales itself to the display. Override it with the "
      "MAGVIEW_SCALE environment variable."),
};

static const MvHelpItem it_cli[] = {
    R("--sim",         "Run against a simulated Explorer that speaks the real "
                       "wire format. Everything in the program works with no "
                       "hardware attached."),
    R("--port DEV",    "Open this serial port at startup."),
    R("--baud N",      "Baud rate for --port (default 9600)."),
    R("--help",        "Command-line summary."),
    R("--help-doc",    "Write this manual to standard output as Markdown."),
    R("MAGVIEW_SCALE", "Override the interface scale, e.g. 2 on a HiDPI "
                       "display."),
};

/* ------------------------------------------------------------------ */

#define SEC(t, intro, arr) { (t), (intro), (arr), (int)(sizeof (arr) / sizeof *(arr)) }

static const MvHelpSection SECTIONS[] = {
    SEC("What this program does", NULL, it_about),
    SEC("Connecting to the instrument", NULL, it_connect),
    SEC("The status strip", NULL, it_status),
    SEC("GPS source", NULL, it_gps),
    SEC("Field — the scrolling strips", NULL, it_field),
    SEC("Chart — the plotter", NULL, it_chart),
    SEC("Log", NULL, it_log),
    SEC("Console", NULL, it_console),
    SEC("Keyboard and mouse", NULL, it_keys),
    SEC("When something is wrong", NULL, it_trouble),
    SEC("Command line and environment", NULL, it_cli),
};

const MvHelpSection *mv_help_sections(int *n_sections)
{
    if (n_sections)
        *n_sections = (int)(sizeof SECTIONS / sizeof *SECTIONS);
    return SECTIONS;
}

void mv_help_write_markdown(FILE *f, const char *version)
{
    int n = 0;
    const MvHelpSection *s = mv_help_sections(&n);

    fprintf(f, "# magview — operator's manual\n\n");
    fprintf(f, "Marine Magnetics Explorer acquisition and display, version "
               "%s.\n", version ? version : "");
    fprintf(f,
        "\nThis file is generated from the help built into the program:\n"
        "`magview --help-doc > docs/HELP.md`, or `make help-doc`. Edit\n"
        "`src/mv_help.c` and regenerate — do not edit this file.\n\n");

    fprintf(f, "## Contents\n\n");
    for (int i = 0; i < n; i++)
        fprintf(f, "%d. %s\n", i + 1, s[i].title);
    fprintf(f, "\n");

    for (int i = 0; i < n; i++) {
        fprintf(f, "---\n\n## %s\n\n", s[i].title);
        if (s[i].intro)
            fprintf(f, "%s\n\n", s[i].intro);

        bool in_table = false;
        for (int k = 0; k < s[i].n_items; k++) {
            const MvHelpItem *e = &s[i].items[k];
            if (e->kind != MV_HELP_ROW && in_table) {
                fprintf(f, "\n");
                in_table = false;
            }
            switch (e->kind) {
            case MV_HELP_TEXT:
                fprintf(f, "%s\n\n", e->a);
                break;
            case MV_HELP_SUB:
                fprintf(f, "### %s\n\n", e->a);
                break;
            case MV_HELP_BULLET:
                fprintf(f, "- %s\n", e->a);
                if (k + 1 >= s[i].n_items ||
                    s[i].items[k + 1].kind != MV_HELP_BULLET)
                    fprintf(f, "\n");
                break;
            case MV_HELP_ROW:
                if (!in_table) {
                    fprintf(f, "| | |\n|---|---|\n");
                    in_table = true;
                }
                fprintf(f, "| **%s** | %s |\n", e->a, e->b ? e->b : "");
                break;
            case MV_HELP_NOTE:
                fprintf(f, "> **Note.** %s\n\n", e->a);
                break;
            }
        }
        if (in_table)
            fprintf(f, "\n");
    }
}
