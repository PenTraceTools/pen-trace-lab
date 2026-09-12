# Comparison lab (0.3.0)

## Purpose and workflow

The 0.2 presets shared a 40 ms future-data window. At slow pen speeds this
limits spatial support before their different radius limits are reached, so
Strong could resemble Gentle. Increasing a displacement cap alone cannot fix
that. Version 0.3 compares distinct smoothing/time/detail trade-offs on the same
recording. It does not claim to recover the physical tip trajectory.

Open any existing `.pentrace`: G shows the first stroke in a six-panel grid.
Use `[` / `]` to select a different stroke. Blue is recorded input, coloured
dashes are the candidate. Each panel uses identical raw bounds and scale.
1–6 selects the candidate for C (normal overlay) or D (difference x8). D displays
`raw + 8 * (candidate - raw)` and explicitly exaggerates offsets; all statistics
and exports still use actual candidate coordinates. Z zooms around the stroke
centre. Grid, difference and zoom pause capture. Return Z to 1x, then use Space
to resume a live recording; loaded recordings require Ctrl+N before drawing new data.

All six candidates are independently evaluated for the selected stroke; all
strokes are evaluated in comparison exports. They are not cascaded. Live paths
are recomputed outside the input callback; expensive scores are finalized on
lift/inspection and cached. This is a diagnostic implementation, not an optimized
production ink engine. Long strokes can increase paint time; watch the paint-call
counter. It is not a pen-to-photon latency measurement.

## Candidates

| Key / ID | Algorithm and default parameters | Important cost |
| --- | --- | --- |
| 1 / local40 | Local-normal, radius 8 DIP, cap 2.5 DIP, window 40 ms | Reference matching 0.2 Steady; provisional tail |
| 2 / local_custom | Local-normal, radius 12 DIP, cap 4 DIP, window 120 ms | More slow-speed support; longer visible revisions |
| 3 / euro_responsive | One Euro variant, cutoff 3 Hz, beta .020, derivative 12 Hz, cap 6 DIP | Causal, but geometric lag and short endings |
| 4 / euro_smooth | One Euro variant, cutoff 1 Hz, beta .005, derivative 8 Hz, cap 10 DIP | More smoothing can lose detail and increase lag |
| 5 / buffer80 | Trailing time-integrated mean, 80 ms, cap 10 DIP | Causal averaging, approximately half-window lag at steady speed |
| 6 / offline120 | Symmetric Gaussian time window ±120 ms, cap 4 DIP | Only finished, uncanceled strokes; not live ink |

Raw is also exported as an identity reference. Local 2 has menu controls for
radii 4/8/12/20 DIP, windows 40/80/120/200 ms and caps 1.5/2.5/4/6 DIP. Settings
changes deliberately reprocess the entire selected stroke. All coordinates
remain logical DIPs, not calibrated millimetres.

Local filtering integrates a polyline over symmetric arc-length support, bounded
by available timestamps on both sides. Only the normal component is applied;
sharp turns and stroke edges reduce correction, and endpoints are preserved.
Its window is the maximum future-data/revision horizon, not a measured display
delay. With fixed settings, points older than that horizon do not change.

The One Euro variants smooth the raw velocity vector, use its magnitude in
`cutoff = min_cutoff + beta * smoothed_speed`, then low-pass position with
`alpha = 2*pi*cutoff*dt / (1 + 2*pi*cutoff*dt)`. A radial displacement cap feeds
back into their state. Isotropic vector speed and this cap are explicit variants,
not a claim of byte-for-byte equivalence to the original scalar reference.
See the [authors' algorithm and tuning guidance](https://gery.casiez.net/1euro/).

The buffered candidate integrates the piecewise-linear raw trajectory over the
preceding time window, including fractional boundary segments. Startup uses only
available history. It is not a delayed output queue. Neither causal candidate
is artificially pulled to the measured endpoint on lift: the exported endpoint
error exposes the cost instead of concealing it with an invented tail.

Offline Gaussian uses 25 time-uniform quadrature nodes with trapezoidal endpoint
weights and `exp(-4.5*u*u)` weights. Its support shrinks near each valid run's
ends and correction fades smoothly there. Run endpoints remain exact. A clean
pen-up is required for availability; canceled/unfinished strokes report N/A.
This is an offline comparison ceiling candidate, not guaranteed optimal output.

Every algorithm resets at unusable motion intervals (nonpositive time, >50 ms
gaps, untrusted clocks, pointer/device/coordinate-mapping changes). No filtering
crosses such a boundary. Raw samples and pressure/tilt fields are never rewritten.
No guide snapping, prediction, global line fitting or automatic straightening is
used in any candidate. Grey guides do not supply ground truth to the filters.

## Interpreting metrics

- Local variation at 5, 10 and 20 DIP: RMS normal deviation from short local
  chords, sampled every .5 DIP of each path's own arc length. This is multiscale
  geometric variation, not a hardware noise estimate. Curves, corners and small
  writing legitimately produce variation. Different resulting path lengths mean
  it is not a point-correspondence accuracy score. Invalid runs suppress it.
- Displacement RMS/max and endpoint displacement: distance from the same raw
  report, not from the unknown physical tip. Large changes can erase intent.
- Nearest-path lag mean/P95: search the preceding 250 ms (at most 128 segments)
  of raw trajectory for a nearer projected position, excluding speeds <5 DIP/s
  and invalid intervals. This is a geometric proxy with zero for ties; crossings,
  loops, noisy motion and the bounded search can make it ambiguous. It is not
  hardware, event-to-display, or physical latency. It does not measure local
  tail revision, whose separate horizon is exported as `window_ms`.
- Raw sharp-turn displacement: maximum point displacement where two-report
  chords turn over 60 degrees. Sample-rate-sensitive heuristic; not a certified
  corner detector. N/A when no such vertices exist.
- Near-closed loop area ratio: signed polygon-area ratio when raw endpoints
  are within 5 DIP and absolute raw area exceeds 1 square DIP. The implicit
  closing chord and winding affect it. Not proof a stroke was an intended loop.

Never rank every stroke by lowest variation alone. A collapsed loop or flattened
letter could win that score. Compare slow/normal/fast diagonals with axes, arcs,
tiny loops, V/W corners, writing, dots and light-pressure endings. Then test live
feel on the pen device. Smoothness and fidelity cannot both be inferred perfectly
from one noisy reported path without an independent physical reference.

## Exports and provenance

Compare offers summary CSV, per-report paths CSV and a parameter-sweep summary.
All use the full recording, even during replay. Each row identifies version,
stroke, candidate, execution kind (`causal`, `bounded_revision`, `finished_only`),
availability and numeric settings. Summary contains the metrics above and
coverage counts. Paths contains original sequence, normalized report time,
clock-recovery flag, raw and candidate X/Y. Unavailable results have blank fields.
The sweep includes the six current candidates plus 12 local combinations,
six One Euro combinations and two buffered windows (26 plus raw); repeated
parameters with distinct IDs are intentional baseline controls.

`.pentrace` remains format 1. Raw reports and the event log are the source;
derived paths are reproducible, not duplicated in the recording. New sessions,
live settings changes, resuming and saving record
`Comparison v0.3.0 local <radius> <window_seconds> <cap>` events. Reopening restores
the last valid setting and reprocesses all strokes with it, not a historical
per-stroke setting timeline. Versioned fixed candidates complete the provenance.
Exports always contain the actual settings even if a loaded recording is not
resaved. Keep CSV plus original recording when comparing software revisions.

The console analyzer is read-only and shares the GUI core:

```text
pentrace_analyze.exe "recording.pentrace" --compare
pentrace_analyze.exe "recording.pentrace" --paths
pentrace_analyze.exe "recording.pentrace" --sweep
pentrace_analyze.exe "recording.pentrace" --legacy
```

Default is `--compare`; redirect stdout to a new CSV if desired. Legacy Session
metrics/motion exports still use the legacy preset, regardless of the selected
comparison candidate. No recordings are uploaded by the application.
