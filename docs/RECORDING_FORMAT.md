# Recording format v1

`.pentrace` is UTF-8, line-oriented text with locale-independent numeric fields.
It is not a native Windows structure dump: padding, pointer width and architecture
do not affect it. Doubles are written with 17 significant digits. Metadata/event
strings use C++ `std::quoted` escaping for quotes/backslashes; line breaks become
spaces on save. The application does not accept partial recordings as complete.

App 0.4 writes this same version-1 source format. Opening an older file does
not alter it. Saved clock calibration may repair older receipt fallback in the
derived analysis only. Reported-samples CSV still exports original stored times.
Analysis exports identify version 0.4.0 and any derived clock recovery. Local
variation is unavailable for short/over-limit paths and invalid motion runs;
it is not a hardware-error metric. See the export sections below.

```text
PENTRACE 1
M "user notes"
S <numeric fields below>
E <relative time> <Windows error code, or 0> "event description"
END <sample record count> <event record count>
```

All sample lines precede event lines when written. `sequence` is an acquisition
sequence, not a hardware report sequence. It can continue across new sessions
within one app process. Events are separate and associated by time. Bounds:
32 KiB per line, 500,000 samples and 50,000 events. Unknown record types, invalid
enums/booleans, nonfinite values, missing END and mismatched counts are rejected.

Real recording sessions include environment events with the QPC origin/frequency,
window DPI, initial canvas dimensions, native processor architecture and app
version. Synthetic demonstrations are explicitly labeled and do not claim a
hardware clock calibration. Test changes are recorded as events during capture.

## S fields, in order

1. sequence, device, qpc
2. pointer, frame, message, flags, buttonChange, milliseconds
3. historyCount, advertisedHistoryCount, batchIndex
4. mask, toolFlags, pressure, tiltX, tiltY, rotation, orientation
5. kind, clock, time, receipt, p.x, p.y
6. coordinates[0..7]
7. rectangles[0..15]
8. originX, originY, dpi
9. mapped, contact, down, up, canceled, eligible, boundary

`kind`: 1 pen, 2 touch, 3 mouse. `clock`: 1 QPC, 2 milliseconds, 3 receipt fallback.
Boolean fields are 0 or 1.

- `device` is a process/session-local Windows source-device handle value. It is not
  a portable persistent serial number or a pointer to dereference when loading.
- `qpc` and `milliseconds` are original Windows timestamp fields. `time` is the
  chosen report time relative to app startup; `receipt` is acquisition time.
- `historyCount` is the number actually retrieved for this batch;
  `advertisedHistoryCount` is the original report's historyCount. `batchIndex` is
  zero-based oldest-first processing order. Latest-only fallback is also logged.
- `mask` is PEN_MASK or TOUCH_MASK as appropriate. A field without its validity
  bit is unavailable, regardless of its numeric value. Pressure is Windows'
  reported normalized value, typically 0..1024; no physical force is inferred.
- `toolFlags`, tilt, rotation and orientation preserve device-type-specific fields.
- Coordinates are original pixel X/Y, pixelRaw X/Y, HIMETRIC X/Y, HIMETRICRaw X/Y.
- Rectangles are device L/T/R/B, display L/T/R/B, touch-contact L/T/R/B and raw
  touch-contact L/T/R/B. Absent mapping/touch fields remain zero; consult `mapped`,
  `kind` and `mask` before interpreting them.
- `originX/Y` is the client screen origin in pixels, and `dpi` is the window DPI.
- `p` is a floating-point canvas position in DIPs. Canvas origin is client DIP
  (20,90). Inspection zoom never changes these saved coordinates.
- `eligible` means the point was within the drawing canvas when acquired. It
  controls whether a new contact can start a displayed stroke.
- `boundary` marks an application-generated termination, not a hardware report.
  Other fields on a boundary may be copied from the preceding report for identity.

The format retains the fields used by this diagnostic, not every possible Windows
or vendor HID field. Keyboard modifier state and arbitrary vendor extensions are
not currently captured. Original reports include hover/up and duplicates;
normalized contact-only strokes are derived again during replay.

Sample CSV is a convenient subset, not a lossless substitute for `.pentrace`.
Comparison CSV analyzes the full recording, including during partial replay.
See [COMPARISON.md](COMPARISON.md) for candidate and metric semantics.

## Motion and speed exports

Session > Export reported motion / speed CSV derives velocities from **normalized contact
strokes**, keeping each pointer/device separate and excluding duplicate reports
and hover/up samples. It exports raw positions, X/Y velocity and speed, along with source sequence, timing source and validity status.
The original `.pentrace` format remains version 1: derived speed is recomputed
from its original positions/timestamps and is not written over them.

For a usable interval ending at sample i:

`velocity = (position[i] - position[i-1]) / (time[i] - time[i-1])`

`speed = hypot(velocity.x, velocity.y)`

Units are DIPs/second. X is rightward and Y downward. No assumption about physical
millimeters, force, or independently measured tip speed is made. Noise in the
position stream can increase apparent speed; timing quantization affects it too.

An interval is unavailable if it is the first sample, has nonpositive time, spans
more than 50 ms, uses a receipt-time fallback, changes clock source, changes
pointer/device, changes coordinate mapping/DPI/origin, or contains invalid data.
CSV uses a reason in `speed_status` and blank velocity/speed cells, not zero.
An actual stationary pen with usable timestamps correctly produces zero speed.
These validity checks do not prove that the underlying reports are physically
accurate. Millisecond clocks have lower precision than QPC clocks.

The sidebar reports raw last/mean/P95/max speed; comparison summary CSV includes
raw mean speed and invalid-interval counts. Mean speed is total distance over
usable intervals divided by their total duration. It is time-weighted, while
the sidebar P95 is interval-weighted. Speed is not independent physical truth.

## Version 0.4 analysis exports

The reported-motion CSV has 16 columns: stroke, sequence, device_session_id,
pointer, kind, time_seconds, clock_source, raw_x_dip, raw_y_dip, dt_seconds,
speed_status, raw_vx_dip_per_s, raw_vy_dip_per_s, raw_speed_dip_per_s,
analysis_clock_recovered, analysis_version. Version is 0.4.0; no preset or
filtered-path columns remain. The first sample has blank dt/velocity/speed;
unusable subsequent intervals retain dt and their reason but blank velocity/speed.

Comparison summary/path/sweep CSV carries version 0.4.0 and complete candidate
settings. See [COMPARISON.md](COMPARISON.md). The source format remains v1;
comparison settings are optional events. Both 0.3 and 0.4 settings are accepted
when opening old recordings. No source reports are rewritten by analysis.
