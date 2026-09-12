# Verification and hardware acceptance

## Current verification status

Source implementation and a source-level review are complete for this initial
version. The source-inventory/manifest/PowerShell checks can run without a build.
The C++ test executable and Windows GUI have **not** been compiled or run on the
authoring machine, respecting the instruction to build on the other PC.

The following are provided, not claimed as executed here:

- CTest core suite: pointer lifecycle, exact duplicates, equal-time state changes,
  pointer isolation, recovered starts, filter identity/rotation/displacement,
  synthetic noisy diagonals at 60/120/240 Hz, gap behavior, line metrics, curve
  deviation, lossless serialization, malformed/truncated files and history retries.
- Windows x64/ARM64 CI builds and host-compatible core test execution.
- Linux core tests with address and undefined-behavior sanitizers.
- Speed tests: vector units, stationary versus unavailable speed, time-weighted
  means, timestamp gaps, clock/pointer/DPI transitions and CSV column consistency.

Run `scripts/verify-source.ps1` for source-only checks. Run the build helper on the
authorized build PC for actual C++ compilation and CTest. Record the commit, build
architecture, Windows version, test output and any warnings before publishing.

## Software smoke test after building

1. Launch the portable executable as a normal user; no installer/admin prompt.
2. Choose Real-pen tests > Diagonal down. Grey targets should appear but the
   report/stroke count must not increase merely from selecting a test. Draw with
   your real pen. Use F2/F3 to change test and pace, and verify the event log.
3. Switch Off/Gentle/Steady/Strong. Off should display your reported path unchanged;
   the filter overlay should change while the blue original remains fixed.
4. Toggle sample dots and curve overlay; inspect at 1x/2x/4x. Verify returning to
   1x is required before resuming capture.
5. Replay at 1x and 4x; verify final frame and last stroke appear. Show full recording.
6. Save, reopen and re-export; compare all numeric source data and metric CSV.
7. Try an invalid/truncated file. It must show an error and leave the prior recording
   available. Cancel a save dialog and unsaved-data prompt; data must remain.
8. Try an unwritable output path. It must report failure, not claim to have saved.
9. Exercise menus by mouse, keyboard and pen. Check text and sidebar layout at your
   actual display scaling. Inspect the sidebar on the smallest supported window.
10. Close with unsaved data; verify Save / Discard / Cancel behavior.

### Two-path and speed checks

- With Raw and Filtered enabled, select Gentle/Steady/Strong: reported input must
  remain solid blue and the algorithm comparison must be dashed orange. Zooming
  must not alter source points or speed values. Short strokes/dots may be too
  short to show a complete dash pattern; that is styling, not lost samples.
- With Off, the two paths coincide and should be drawn once. Hiding Raw while
  leaving Filtered enabled must still show the coincident path.
- Verify exact first/last measured positions in every filter mode. During live
  input, inspect the trailing 40 ms revision; do not mistake it for immutable ink.
  Check right-angle vertices, tiny loops and reversals for loss of intended detail.
- Open a version-0.1 recording with saved clock calibration and legacy fallback.
  Check recovered-clock counts; sample CSV must retain original saved timestamps,
  while motion CSV marks recovered analysis times. Save a copy and compare source
  numeric fields. Original coordinates must remain identical.
- Select the stationary-hold test. Hold on a target for 5 seconds at two pressures.
  Grey targets are reference visuals only, never proof of physical position.
- Export motion / speed CSV. Verify raw and filtered positions/velocities are
  available together. First samples and unusable intervals must have an explicit
  status and blank speed/velocity fields, never invented zero values.
- Draw comparable slow/fast trials and inspect the speed distributions. Do not
  treat a noisy coordinate derivative as independently measured physical speed.
- Compare metric CSV in Off and another mode. Its speed describes the selected
  path, while the sidebar speed describes the reported input. Motion CSV contains
  both. Check status/count/coverage fields before comparing numeric summaries.

## Pen/touch input checks on the target device

- First set Filter > Off, and enter exact device/pen models and relevant firmware
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
- Repeat under rendering load, with dots/curves both on and off. Look for history
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
