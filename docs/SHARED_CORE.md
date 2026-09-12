# Shared local-correction core (0.4.1)

[Pen Tools overview](https://github.com/alexiokay/pen-tools) connects the repositories.

PenTraceLab, the InfiniPaint `graphite-ui` integration and
[upstream proposal #98](https://github.com/ErrorAtLine0/infinipaint/pull/98) consume
[pen-stabilizer](https://github.com/alexiokay/pen-stabilizer), package 0.1.0,
algorithm revision 1, through a Git submodule pinned to
`adbdce4e902433fcd14fba16e08863f2ec909f79` (v0.1.0). There is no additional
runtime DLL, executable, service or prerequisite installer.

After pulling source on your build PC:

```sh
git submodule update --init deps/pen-stabilizer
```

The previous `localFilter` implementation has been replaced by an adapter to the
shared production header. PenTraceLab still handles contact processing, clock
recovery, identity/coordinate boundaries, diagnostics, replay and other comparison
algorithms. Invalid timing/provenance splits correction runs. Recorded raw
samples, pressure, speed calculations and the five-candidate comparison remain.

The diagnostic's full parameter range is retained. The library is not silently
clamped to InfiniPaint's narrower UI range. Filtering uses immutable source
samples and the same live-tail semantics as the drawing integration; lifting
does not perform another beautification pass.

Comparison CSVs now identify core package, algorithm revision and coordinate
units. Non-local candidates leave the core identity columns empty. The app
version is 0.4.1; old 0.3.0 and 0.4.0 comparison settings remain readable. These
are derived export additions, not a replacement raw recording format. New
session metadata identifies the core; raw device reports remain untouched.

Library CI independently compares its output to pinned PenTraceLab 0.4.0 source
at commit `ef6555a6defd12b8dde5afc408df4975eb4492b2`. Do not replace that oracle
with a checkout that calls the shared core: that would compare code to itself.

The diagnostic adapter uses the core's efficient batch entry point, evaluating
the same position math once per output point rather than replaying every
intermediate tail. Completed comparisons are cached; active stroke comparisons
are still recomputed. High-rate long-stroke performance and physical-pen acceptance
need measurement. Pure-core tests do not establish GUI latency or hardware
accuracy. The executable is built by CI/on the build PC, not this development PC.

The app and library are versioned independently. Updating the library repository
does not update an installed executable or a consumer's pin. See the
[update workflow](https://github.com/alexiokay/pen-tools/blob/main/VERSIONING.md).
The upstream PR is a draft proposal, not an already-merged upstream feature.
