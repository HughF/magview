# Explorer serial protocol

Notes on what `magview` reads off the wire, and how confident we are in each
part. Implemented in `src/mv_proto.c`; exercised by `tests/test_proto.c` and
by the simulator in `src/mv_sim.c`.

## Link

- **9600 baud, 8N1** by default. The port picker offers 4800–115200 for units
  configured otherwise.
- Lines are terminated with CR, LF or CRLF. The assembler
  (`mv_lineasm_feed`) tolerates all three and splits on any of them.

## Magnetometer output

The Explorer, and its SeaSPY / SeaQuest siblings, can be set to emit several
telemetry strings. Two forms cover the family, and both are read:

### Tagged form

```
*YY.JJJ/HH:MM:SS.S F:FFFFFF.FFF S:SSS D:+DDD.Dm A:AAA.A L:L TTTms_Q:QQ !!!! <CR><LF>
```

| field | meaning |
|-------|---------|
| `YY.JJJ` | year . day-of-year |
| `HH:MM:SS.S` | time of the reading |
| `F:` | total field, **nanoTesla** |
| `S:` | signal strength (raw). >800 acceptable, >1300 excellent |
| `D:` | towfish depth, metres (signed) |
| `A:` | altitude above the seabed, metres |
| `L:` | leak flag (0/1) |
| `Q:` | quality indicator |
| `!!!!` | fixed end marker (not a checksum) |

The reader **locates each `X:` tag wherever it sits** rather than trusting
fixed columns, because the family glues tokens together (`120ms_Q:07`). A tag
that is absent yields "not available" (blank in the log), never a zero.

### Legacy form

```
$ FFFFFF.FF  SSS  DDDD <CR><LF>
```

The character immediately after `$` is a flag: a **space** when the field is
below 100000 nT, or **`1`** when it is at or above — that `1` is the
hundred-thousands digit of the field. Skipping only the space keeps the `1`,
so the first whitespace-delimited number is the whole field either way. The
remaining numbers are signal, then **depth in units of 0.1 m**.

## GPS

The Explorer can pass an auxiliary GPS feed through the same serial line.
`$GPGGA`, `$GPRMC` and `$GPGLL` (any talker — GP/GN/GL) are parsed for a fix,
and the most recent fix is attached to each mag reading, which is what
georeferences the log and draws the plotter track. `ddmm.mmmm` coordinates are
converted to signed decimal degrees; a GGA fix quality of 0, or an RMC/GLL
status of `V`, is treated as no fix.

## Checksums

An NMEA `*HH` checksum, when present, is the XOR of the bytes between the
leading `$`/`*` and the `*`. A line whose checksum fails is **still parsed**,
with the failure recorded and counted — losing a fix costs a position, and
every field is range-checked independently. The tagged mag form's leading `*`
is explicitly **not** treated as a checksum delimiter (an early bug: it made
every good line read as a bad checksum).

## Confidence

The tagged and legacy layouts, the baud rate, the `$`→`1` field flag and the
0.1 m depth scaling come from the Explorer / SeaSPY2 operation manuals. What we
do **not** have is a byte-faithful capture from a real instrument, so the
parser is deliberately tolerant and the tests pin behaviour against the
simulator and the manuals rather than against a golden capture. **The day a
real capture exists, turn it into a golden test** and tighten anything that
disagrees.
