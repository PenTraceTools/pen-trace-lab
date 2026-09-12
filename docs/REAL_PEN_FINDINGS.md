# First real-pen recording and 0.2 changes

The first user-supplied recording contained 11,299 reports and 18 contact strokes.
The private recording itself is not committed or uploaded with this source repo.

## Observations (not hardware ground truth)

- 1,312 reports, including 857 contact points, had selected receipt-fallback time.
  Their original QPC fields were preserved. The reported QPC was about 5–23 ms
  ahead of receipt, while contact intervals remained mostly 3–4 ms. The previous
  5 ms future-time rule destroyed that useful cadence and introduced a backward
  step when changing clocks. The cause of the clock offset remains unproven.
- Mapping context was constant, coordinates had fractional HIMETRIC precision,
  and pen raw/non-raw coordinate pairs were identical throughout. No history
  retrieval fallback was logged. This does not prove no device reports were lost.
- Two long strokes around 230 DIP/s showed local sideways variation of roughly
  .392 DIP diagonal versus .122 DIP vertical (10-DIP chord metric). Hand motion
  remains part of these measurements; these are not electrical-error estimates.
- The old Strong comparison reduced that diagonal's local variation by about
  46%, but ended about 8.07 DIPs from the last recorded contact point. Stronger
  smoothing alone therefore did not meet the endpoint/responsiveness requirement.

## Implemented response

Retain compatible QPC timing independently of receipt offsets; log clock-offset
warnings without flooding the event log. Recover old analysis timestamps only
with saved calibration and retain original files/reports unchanged. Reject invalid
filter intervals instead of using callback batching as sample cadence.

Replace the lagging comparisons with bounded local-normal smoothing, measured
endpoints, corner attenuation and an explicitly provisional 40 ms tail. Add real-
pen guides and local-variation metrics. Remove the synthetic demo from the app;
keep synthetic cases in regression tests to check software invariants.

These are diagnostic improvements, not a guarantee of perfect intent recovery.
Slow diagonals, small handwriting, repeated trials, and rendering/compositing in
the actual painting app remain necessary before integrating this candidate there.
