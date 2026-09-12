# Design, findings and next integration steps

## Measurement boundary

The application observes Windows-reported data. "Reported" and "raw" here do
not mean raw electrode voltages or electromagnetic waveforms. The usual chain is
physical pen/digitizer interaction, device electronics/firmware, digital reports,
Windows input processing, application coordinates, optional filter and renderer.

The details of the early stages depend on the device technology. A wavy Windows
report therefore implicates a stage before application filtering, but does not
isolate hardware from hand motion, firmware or drivers. Coordinate mapping also
needs to be checked against the retained pixel and HIMETRIC fields.

## Can electrical/internal pen data be measured too?

Sometimes manufacturers have engineering tools, diagnostic firmware or documented
vendor interfaces for additional measurements. Ordinary drawing apps do not have
a universal Windows API for all internal sensor readings.

Windows does expose `GetPointerDeviceProperties` and `GetRawPointerDeviceData` for
properties a device reports. Microsoft documents access to additional usages,
including vendor-specific ones, when they are in the same report as X/Y. This may
provide useful extra **digital report fields**. It does not imply access to the
underlying electrical signal or a more accurate coordinate stream.

Sources:

- [GetRawPointerDeviceData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawpointerdevicedata)
- [Supporting usages in digitizer report descriptors](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/supporting-usages-in-digitizer-report-descriptors)
- [Integrated pen report fields](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/required-hid-top-level-collections)

This version does not capture additional HID properties. A later read-only
capability inspector is reasonable if the baseline leaves a specific unanswered
question and the device exposes useful documented fields. It should report
unsupported/access-denied cases honestly, not install a driver to bypass them.
Direct electrical measurement can require manufacturer knowledge and specialist
equipment. Opening or probing the device is outside this software project's scope.

## Acquisition

`WindowsInput` is the only pen/touch acquisition route. It receives `WM_POINTER`
and reads the appropriate pen/touch history before the next message is retrieved.
`readHistory` is a platform-neutral, fake-API-testable implementation of the
count/query/retry/fallback behavior. It emits oldest-first and never separately
appends the newest sample to a successful history result.

History fallback is logged. Both returned batch size and device-advertised history
count are saved. Original reports are retained before normalization. The recorder
does not pump UI messages or write files during a history read.

The normalized view deduplicates equivalent report content with source identity,
frame/timing, flags and attributes, not timestamps alone. It remembers the most
recent 256 reports per pointer; unusually long overlapping histories can exceed
that window and must be investigated through the original recording. Without a
report clock, equivalent positions are retained rather than assumed duplicate.

Pointer/device identity separates strokes. Only in-contact reports form the
diagnostic polylines. Up/hover/cancel reports remain in the file but do not produce
a synthetic tail. A start recovered from in-contact input without DOWN is labeled.
Loss of focus/capture, resize, DPI changes and pause generate marked application
boundaries. These markers must not be interpreted as measurements from hardware.

Ordinary pen/touch reports outside the canvas are retained. A stroke can begin
when an in-contact pointer enters the canvas, labeled as a recovered start. Mouse
capture is off by default and explicitly opt-in. Touch hiding affects rendering,
not acquisition. Palm contacts suppressed before delivery cannot be recovered.

Sources: [pen history](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerpeninfohistory),
[touch history](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointertouchinfohistory),
[capture changes](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointercapturechanged).

## Coordinates and clocks

The manifest declares per-monitor-v2 DPI awareness. Device/display rectangles map
HIMETRIC raw positions to fractional screen pixels; unavailable mapping falls
back to reported raw screen pixels. Client origin, DPI and mapping rectangles are
retained. Client coordinates are converted to canvas DIPs (client offset 20,90).
DIPs are not physical millimeters. No axis-dependent smoothing is applied.

Windows documents predicted/non-predicted coordinate fields as equivalent for pen
input. Touch may differ, so both are recorded and the non-predicted fields drive
the baseline. No claim is made that selecting one field fixes calibration.
[POINTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info),
[device rectangles](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerdevicerects).

QPC timestamps are mapped to process-relative seconds using integer subtraction
before conversion to double. Report and receipt are separate clocks for analysis:
a small positive offset is flagged, not replaced with callback arrival time.
Offsets above 5 ms are reported once per device/condition; a gross incompatibility
over 60 seconds is rejected. These are diagnostic bounds, not Windows standards.
Millisecond reports use a receipt-relative, wrap-aware fallback. If neither report
clock is usable, receipt time is retained for replay only and affected filter/speed
intervals remain unavailable. Nonpositive report intervals, clock/mapping changes
and gaps over 50 ms split filtering windows rather than creating invented cadence.

Old recordings retain original QPC fields and a calibration event. A validated,
unambiguous origin/frequency lets Processor recover legacy receipt-fallback times
in a derived copy. The Session and reported-samples CSV keep the original values.
Recovery is counted and marked in motion CSV. Conflicting/missing calibration
disables recovery. Synthetic boundaries never receive a recovered hardware time.

Replay follows receipt times to reproduce batches approximately; filters use
report times. Replay on a different display does not recreate the original
physical pen/display latency. A 16 ms UI timer requests redraws; Windows/GPU load
can change actual frame timing.

## Analysis, filtering and rendering

The only analysis mode is the independent-candidate comparison described in
[COMPARISON.md](COMPARISON.md). Raw input is an identity reference; the five
candidates each start from it. Local-normal smoothing, One Euro variants,
trailing averaging and an explicitly offline Gaussian comparison have separate
execution/lag/shape trade-offs. There is no preset-mode enum or curve-fit renderer.
The old Off/Gentle/Steady/Strong view and exports were removed in version 0.4.

Input timestamps, mapping and contact segmentation remain separate from every
filter. Local filters reset across invalid timing/mapping intervals and preserve
run endpoints. Causal filters expose endpoint lag instead of inventing a tail.
The reported-motion export uses only original normalized positions and valid
report intervals. Summary/path/sweep exports identify each candidate and settings.

Direct2D renders raw polylines and coloured dashed candidate polylines. Dashes
are visual styling, not omitted reports. G displays five equal-scale panels;
D exaggerates candidate displacement by 8x and is explicitly inspection-only.
Metrics and CSV paths never use the exaggerated geometry. Touch and pen have
separate streams; hiding touch does not delete its recorded reports.

## Microsoft's moving-jitter test

Microsoft's published hardware-validation procedure measures moving pen deviation
and reporting rate using specified fixtures. It is not a Windows stabilization
setting. Our freehand straightness measurements are not equivalent to that test,
its fixture or certification. The app does not issue a Windows compliance verdict.
[Microsoft Moving Jitter](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/moving-jitter).

## Safety, bounds and known limits

- No network, telemetry, drivers, calibration writes, firmware commands or admin privileges.
- No raw HID/electrical acquisition in this version.
- Up to 500,000 reports, 50,000 log events, 1,024 normalized pointer identities and
  20,000 normalized strokes per recording. Start a new session for long trials.
- Original file loading is bounded and transactional. Unknown versions, invalid
  field values and truncated recordings are rejected before replacing current data.
- Saves use a same-directory temporary file and replacement only after a successful
  write/close. Output paths requiring more than the temporary-file API supports
  fail safely; choose a shorter directory. Atomic visibility is not a guarantee
  against every power-loss/storage failure.
- Geometry is cached at the point-array level, not the GPU geometry level. Large
  selected strokes or many sample dots can slow painting. Watch the paint-call
  duration and compare with overlays disabled. All history is still retrieved;
  API fallback/gap evidence must be checked before treating a recording as complete.
- No automatic hardware/driver/hand-motion classification, calibrated-mm readings,
  hardware scan-rate guarantee or input-to-photon latency measurement.
- Renderer and input backend still require compilation and physical-device testing.

## After collecting recordings

1. Verify mapping, ordering, clock quality, capture boundaries and history fallbacks.
2. Compare slow/fast strokes, orientations and screen positions; repeat trials.
3. Correct acquisition defects before evaluating smoothing strength.
4. Replay identical data through filter candidates; evaluate detail and endpoints
   alongside straightness. Do not tune only for perfect synthetic straight lines.
5. Replay through InfiniPaint's actual renderer to isolate its geometry effects.
6. Integrate a tested input path, reversible source samples and bounded optional
   smoothing. Keep prediction preview-only if later introduced.
7. Validate brush pressure/taper, transparency, erasers, gestures, save/reopen,
   undo/export and collaboration before producing a final portable fork build.

There is no guaranteed algorithm for complete noise removal with perfect intent
preservation. Device/driver investigation may be necessary when baseline input
cannot meet the user's tolerance without distorting deliberate detail.
