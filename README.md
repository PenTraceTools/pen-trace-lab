# Pen Trace Lab

A small native Windows pen/touch recorder and drawing diagnostic. Compare the
positions Windows reports with filtered polylines and an experimental curve
renderer, using the **same recorded stroke** for every comparison.

**Status: experimental diagnostic, version 0.2.0.** The initial ARM64 version was
used to collect a real pen recording. This revision addresses its clock-handling
findings and replaces the lagging comparison filters. See [VALIDATION.md](docs/VALIDATION.md)
for verification boundaries. No hardware-accuracy or complete wobble-removal claim.

## Features

- Native C++20 / Win32 input, Direct2D drawing and DirectWrite text.
- Pen and touch history capture with chronological replay, retry on insufficient
  history buffers, explicit fallback logging and optional mouse capture.
- Reported coordinates, timestamp source, pressure/tilt validity, pointer/device
  identity, contact state and coordinate-mapping context retained in recordings.
- Distinct pen/finger strokes; no positional filtering in the initial baseline.
- Off is exact. Gentle, Steady and Strong use local sideways smoothing, with
  displacement caps of 1.5, 2.5 and 4 DIPs. Endpoints stay measured. The newest
  40 ms may revise as input arrives; this is not a causal, final-ink algorithm.
- Straightness RMS/P95/max, filter displacement, interval statistics, endpoint
  displacement and sampled experimental-curve deviation.
- Report-derived speed and X/Y velocity: last interval, time-weighted mean, P95,
  max, coverage/rejection counts and per-interval CSV export.
- Guided test prompts, original-sample dots, inspection zoom and stroke selection.
- Local recording save/load, 1x/4x replay, sample and per-stroke metric CSV export.
- Real-pen tracing guides, slow/normal/fast labels and stationary-hold targets.
  Guides are never recorded or used to snap/correct the pen trajectory.
- Portable packaging for x64 and ARM64, automated core tests and CI configuration.

This is a diagnostic, not a replacement painting application. There is no
prediction, shape snapping, pressure-shaped brush, network service, telemetry,
driver installation, firmware modification or system setting change.

## Build on your other Windows PC

See [BUILDING.md](docs/BUILDING.md) for exact prerequisites. With Visual Studio
2022 C++ tools, the appropriate architecture toolset, Windows SDK and CMake:

```powershell
./scripts/build.ps1 -Architecture ARM64
```

For an Intel/AMD target, use `-Architecture x64`. The ARM64 package is for the
ARM64 drawing device even if you compile it on an Intel/AMD PC. The script runs
host-compatible core tests and creates `out/PenTraceLab-ARM64.zip`. It never
installs prerequisites. Extract the ZIP and run `PenTraceLab.exe` on the target.

## First recording

1. Open **Session > Device and test notes**. Enter device, pen, test speed and
   whether a physical guide was used. Notes editing pauses capture; press Space
   afterward to resume.
2. Leave **Filter > Off**. Choose **Real-pen tests > Diagonal down**, then draw slowly in
   both directions. Repeat at normal/fast speed and different screen positions.
3. Also record horizontal/vertical lines, shallow curves, corners, small writing,
   dots and pen lifts. Use separate files or notes to identify trials.
4. Pause and save a `.pentrace` recording. Saving pauses capture intentionally.
5. Compare filters or use **Capture / replay**. Original reports are unchanged.
   Enable **View > Reported sample dots** and inspection zoom for close analysis.
6. Export sample CSV for report-level analysis, metric CSV for per-stroke summaries,
   and **motion / speed CSV** for reported-versus-filtered velocity at each retained
   contact point. Exports use the full recording and currently selected filter.
   CSV is an analysis subset; `.pentrace`
   retains the full captured fields and event log.

Solid blue is reported pen, green is finger input, **dashed orange** is filtered
position and purple is the optional curve experiment. Select filter 1–3 with both
View layers enabled to compare the two paths. With Off, the paths coincide and
are shown once. Dashes are visual styling only; no source points are removed.
Mouse recording/display is opt-in.
Grey dashed shapes are **targets for you to trace**, not generated pen strokes.
F2 selects the next real-pen test; F3 changes the intended pace label. These
changes are logged during live capture; they do not clear existing strokes.
Use View > Show only selected stroke to inspect overlapping trials without deletion.
Hiding finger strokes does not stop their recording. The last 100 strokes plus
the selected stroke are drawn; all recorded strokes remain available for export.

Keys: Space pause/resume, 0–3 filter, `[`/`]` selected stroke, Z inspection zoom,
Ctrl+S save, Ctrl+O open, Ctrl+N new, F1 help. Mouse wheel or Page Up/Down scrolls
the statistics sidebar. Session > Recent diagnostic events shows the latest log
entries. Zoom pauses capture and must return
to 1x before capture resumes. Loaded sessions do not accept new pen data;
start a new recording to draw again.

## How to interpret the result

Waves in the reported polyline existed before this app's filter and curve renderer.
They might still be hand motion, firmware/driver behavior, mapping issues or
device noise. The app cannot infer your intended physical path.

Straightness only measures deviation from a fitted line. It is not meaningful as
a quality score for an intentional curve. Always examine displacement and detail
loss alongside wobble reduction. DIPs are logical screen units, not calibrated
millimeters. Paint-call duration is not pen-to-photon latency.

Speed is derived from reported position differences and report timestamps, not
an independent physical measurement. Digitizer noise can inflate it. Unusable
intervals have an explicit status and blank CSV velocity/speed values, not a fake
zero. See [the recording/analysis semantics](docs/RECORDING_FORMAT.md).

Version 0.1 recordings with a valid saved QPC calibration can recover report
timing in the analysis view. The original `.pentrace` is never rewritten by
opening/replaying it. The sidebar shows recovered-clock counts. A clock offset
between report and receipt is not an input-latency measurement.

The portable package also includes a read-only console analyzer:
`pentrace_analyze.exe recording.pentrace`. It runs the same filters/metrics as
the GUI and prints a comparison without opening a window or modifying the file.

- [Architecture, findings and electrical/HID limitations](docs/DESIGN.md)
- [Independent trackers, magnetic overlays and correction-layer feasibility](docs/INDEPENDENT_TRACKING.md)
- [Recording format and field semantics](docs/RECORDING_FORMAT.md)
- [Tests and physical-device acceptance checklist](docs/TESTING.md)
- [What has and has not been verified](docs/VALIDATION.md)
- [Build and publishing instructions](docs/BUILDING.md)

## Privacy and publishing

All data remains local. Recordings can contain handwriting, notes, screen
coordinates and session-local device handles. Review them before sharing. Data
files and build outputs are ignored by Git by default. The project is MIT-licensed;
no remote repository is created or published automatically.
