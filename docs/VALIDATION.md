# Validation record — 2026-09-12

## Version 0.4 removal of obsolete modes

Removed the preset enum/API, Off/Gentle/Steady/Strong UI, old 40 ms reference
candidate, curve renderer and old preset-specific CSV/CLI paths. Raw motion
export remains with a simpler 16-column schema. Five independent candidates
remain; Local adjustable is now key 1 and the default. Source recording format
and old clock/settings recovery are preserved. Regression tests were migrated
to the remaining APIs without removing input/motion/filter-boundary coverage.
Build/test this revision via GitHub before using its portable package; previous
0.3 CI results below do not constitute verification of changed 0.4 code.
No local compilation or prerequisite installation is performed.

## Version 0.3 comparison lab

Implementation adds six independent candidates, equal-scale comparison panels,
explicit x8 displacement inspection, adjustable local support, multiscale shape
and lag proxies, versioned settings and all-candidate/sweep CSVs. Core tests add
comparison invariants and exports. Commit b0bd84c passed GitHub run
[34715977560](https://github.com/PenTraceTools/pen-trace-lab/actions/runs/34715977560):
Linux ASan/UBSan core tests and Windows x64/ARM64 builds with host-x64 tests.
All 14 core test groups passed; no C++ compiler warnings were found in the logs.
Minor menu-label/documentation and additional gap-regression follow-ups require
their own passing run before the final portable package is delivered.
No local build or prerequisite installation is authorized or performed. A new
portable version will not replace the user's older app folders or recordings.
Real-device GUI/feel acceptance remains a separate user test.

The downloaded ARM64 analyzer executed on three existing local recordings
(18, 18 and 27 strokes). All 441 default comparison rows were generated;
all raw valid-motion checks had zero invalid intervals, and candidate 2 retained
zero endpoint displacement in every stroke. A 26-candidate-plus-raw sweep on
the 27-stroke session produced 729 rows. SHA-256 checks before/after matched for
all three source files. No recording was uploaded. Default analysis of the
largest recording took approximately 0.77 seconds in one console run; this is
not a live paint benchmark or latency measurement. See
[COMPARISON_RESULTS.md](COMPARISON_RESULTS.md) for measured trade-offs.

## Version 0.2 update (supersedes initial status below)

The public repository is https://github.com/PenTraceTools/pen-trace-lab. The user ran
the initial ARM64 portable app and supplied a real `.pentrace` recording. This
establishes a successful launch/capture on that device, not full hardware acceptance.
The initial source-only entries below describe the earlier creation phase.

0.2 changes: independent report/receipt clock handling and legacy replay recovery,
bounded local-normal filters with unchanged endpoints and a provisional 40 ms
tail, real-pen guides instead of the in-app synthetic demo, local-variation metrics
and a read-only analyzer. Regression cases cover clock-offset recovery, immutable
source data, bounds, endpoints, straight-line lag, rotation/translation, corner
vertices, coordinate transitions, stationary input and guide/sample separation.

Source checks passed locally. GitHub run 34714445835 for commit 3941ae7 passed
Linux ASan/UBSan tests and Windows x64/ARM64 builds with host-compatible tests.
The downloaded ARM64 console analyzer ran on the existing user recording:
11,299 reports, 18 strokes, all 1,312 legacy fallback clocks recovered, zero
nonincreasing contact intervals and zero gaps over 50 ms. All 18 strokes in all
four modes had zero endpoint displacement. Source recording was not modified.

For the comparable diagonal (stroke 8), new Strong local variation was .21827 DIP
versus raw .39209 (about 44% lower), with maximum displacement 1.40182 DIP and
endpoint displacement zero. Old Strong was about .2131 DIP with 8.07 DIP endpoint
displacement. This is a better endpoint/displacement trade-off in this recording,
not proof of a universally better filter or measured physical noise removal.

No compiler/SDK was installed or build run on the authoring PC. The revised GUI
and physical feel still require target-device testing. Subsequent export/UI/test
follow-ups must pass their own GitHub run before shipping the final package.

## Executed on the authoring system

- `scripts/verify-source.ps1`: passed source inventory, local quoted include
  resolution, application-manifest XML parsing and PowerShell script parsing.
- `git diff --check`: passed (line-ending conversion notices are not test failures).
- A source-only lexical delimiter check: balanced C++ braces, brackets and
  parentheses after removing comments/string literals. This is not a C++ parser.
- Reviewed input ordering, fallback behavior, state transitions, coordinate math,
  file replacement, recording limits, replay completion and resource/compiler
  separation. Review led to fixes for final-frame invalidation, DPI-sized startup,
  sidebar scrolling and C++ options accidentally applying to the resource compiler.
- Confirmed this is a separate Git repository with no remote. No original
  InfiniPaint executable or source was modified by this new app implementation.

## Not executed here

- CMake configure, C++/resource compilation, linking or CTest execution.
- The provided GitHub Actions workflow.
- Windows app launch, visual UI verification or real pen/touch input.
- Native ARM64 execution, multi-monitor/DPI acceptance, or hardware latency tests.

No compiler, SDK, package dependency or other prerequisite was installed, and no
executable was produced here. The next verification gate is the build helper on
the authorized build PC, followed by `TESTING.md` on the actual pen device.

Do not interpret this record as a guarantee that the initial implementation is
bug-free or already hardware-validated. Update it with actual results after those
checks; retain failures and known limitations rather than overwriting them with a
blanket success statement.

## Follow-up: comparison styling and speed

Added a solid reported / dashed filtered comparison and explicit report-derived
speed/velocity exports and statistics. Added speed validity and regression tests;
the source-only checks were repeated. This does not change the uncompiled and
hardware-unverified status above. The original recording format is unchanged.

Independent tracking/overlay feasibility and the distinction between physical
reference measurements and report-derived estimates are documented in
`INDEPENDENT_TRACKING.md`.
