# Verification and hardware acceptance

## Verification workflow

See [VALIDATION.md](VALIDATION.md) for executed CI and real-recording checks.
Run source checks without a compiler; build/test only on the authorized build PC
or GitHub. Windows x64/ARM64 builds and Linux ASan/UBSan tests are provided.
Physical pen feel and native GUI acceptance are separate from CI success.

Core regressions cover capture lifecycle, history deduplication, clock/mapping
changes, source immutability, parsing, raw speed validity, local filter geometry,
five-candidate independence, causal-prefix invariance, local revision horizons,
offline availability, caps, stationary input, gap resets, settings compatibility
and rectangular CSV rows. Synthetic fixtures test invariants, not real pen noise.

## Software smoke test

1. Launch normally; no installer/admin prompt. Confirm version 0.4 and candidate
   1 Local adjustable is selected. There must be no Legacy menu, preset choices,
   curve overlay or key that re-enters an old mode.
2. Choose a real-pen guide. Nothing should be recorded until you use the pen.
3. Draw, lift, use G for five equal-scale panels, 1–5 to select, D for explicit
   x8 displacement, C for selected overlay, O for all overlays, [ / ] for strokes.
4. Toggle raw/candidate layers and dots. Return Z to 1x before Space resumes.
5. Save/reopen 0.4 and older recordings. Raw data must remain unchanged; settings
   events restore candidate 1. Loaded files require New before fresh drawing.
6. Export raw samples/motion and all-candidate summary/paths/sweep. Check version,
   candidate IDs/settings and invalid interval blanks. Summary always covers the
   full recording. The removed CLI flag must be rejected.
7. Replay at 1x/4x and show the full recording. Inspect unfinished/canceled strokes:
   offline Gaussian must remain unavailable.
8. Exercise file-dialog cancellation, save failure, malformed recordings, unsaved
   close prompts, menus, sidebar scrolling, display scaling and minimum window.

## Pen/touch input checks on the target device

- Enter exact device/pen models and relevant firmware
  or driver versions in Notes. Record whether the device is charging, the trial
  speed and whether a physical guide is used. Resume after editing notes.
- Confirm the line is under the tip across the canvas, including edges and at
  each display scale/orientation you use. Compare retained raw pixel vs mapped
  HIMETRIC positions if there is an offset or scaling mismatch.
- Draw with pen and multiple fingers. Verify distinct strokes/identities and no
  connection between them. Hide touch; pen acquisition must remain unchanged and
  touch reports should still exist in the file when Windows delivers them.
- Check pressure and tilt validity in the recording, not just numeric values.
  Missing capabilities must not be mistaken for physical zero values.
- Verify hover does not draw, tap produces a dot, lift ends the stroke, and hover
  after lift does not add a tail. Test light pressure and the eraser end/button.
- Leave the canvas while in contact, lift outside the client, alt-tab, open a
  menu, resize/move the window and change DPI. Confirm strokes terminate safely
  and never join across unrelated contacts. Boundary markers should be visible
  in the file rather than disguised as pen coordinates.
- Repeat under rendering load, with dots/candidate overlays both on and off. Look for history
  fallbacks, gaps, invalid records or unusually long paint calls. A smooth-looking
  line alone does not prove that acquisition was complete.

## Drawing-quality trials

Use at least three repeats per condition; do not tune to one lucky stroke:

- Slow/normal/fast diagonals in both directions.
- Horizontal/vertical strokes at comparable speeds.
- Repeats at center and near each screen edge; different natural pen angles.
- Shallow arcs, circles, small loops, spirals, figure eights and tiny handwriting.
- Sharp corners, dots, short flicks and pressure-light endings.

For straightness evaluation, an appropriate non-scratching, non-conductive guide
can reduce hand variation. Do not use anything that risks the display or interferes
with the digitizer. A drawn on-screen guideline alone is not ground truth. This
is not the fixture-controlled Microsoft certification test.

Compare filters on identical recordings. Evaluate straightness, displacement,
endpoints, corners and loop detail together. Keep the chosen filter off when
capturing baseline data; all recorded source samples are unfiltered either way.

Compare another drawing application with smoothing disabled where possible, but
remember its renderer/input processing may differ. A discrepancy does not alone
identify a particular driver or hardware component.

## Release gate

Do not call the app validated until compilation/core tests, software smoke tests,
and the pen/touch/DPI checks above have passed on the intended device. Record known
failures honestly. Passing this diagnostic's tests does not validate InfiniPaint's
brush renderer, persistence or networking; those are a later integration stage.

Neither a particular noise-reduction percentage nor a perfectly straight fitted
line establishes that the true intended freehand path was recovered.
