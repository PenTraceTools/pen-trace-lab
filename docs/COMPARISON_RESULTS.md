# First real-recording comparison (0.3.0)

Read-only analysis with the GitHub-built ARM64 core at b0bd84c. Three existing
sessions were reused locally; no raw recordings were committed or uploaded.
Results below concern the 27-stroke third session. These are Windows-reported
positions, not independently measured physical trajectories.

## Long diagonals

Local variation is RMS in DIPs at the 10 DIP spatial span. Lower variation is
not automatically better handwriting fidelity. The four strokes were chosen
from the earlier long-diagonal review, not by selecting whichever results win.

| Stroke | Raw | Local 40 ms reference | Local 120 ms default | Default reduction vs raw | Default max displacement |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | .49957 | .46617 | .30794 | 38% | 1.503 DIP |
| 4 | .35995 | .28767 | .16970 | 53% | 1.376 DIP |
| 6 | .62417 | .60461 | .44494 | 29% | 1.044 DIP |
| 11 | .51083 | .46720 | .28476 | 44% | 1.396 DIP |

Both local candidates retain exact endpoints. The new default offers more
support at slow speeds, but its provisional tail can revise for 120 ms rather
than 40 ms. Near-zero nearest-path lag does NOT cancel out this revision cost.
It is not yet demonstrated to feel better in live use.

The smooth One Euro candidate gives variation .26235, .17082, .35695 and .24466
on these same strokes. However, endpoint displacement is respectively 9.69,
10.00, 6.56 and 10.00 DIP, with mean geometric lag estimates around 78–121 ms.
This candidate can suppress more waviness but is not a clear overall winner.
The 80 ms trailing average stays near a 40 ms geometric lag on these examples.

## More smoothing is not free

For strokes 1 and 6, the local 200 ms / 12 DIP sweep setting yields variation
.21884 and .33873: approximately 56% and 46% below raw. It still has exact
endpoints, but allows a 200 ms revision horizon and must be tested for unstable
tails or removed detail. It remains a selectable experiment, not the default.

For corner-test stroke 22, raw variation is .69143; local 120 ms gives .65733,
whereas smooth One Euro gives .79785 with 7.56 DIP raw-turn displacement and a
10 DIP endpoint offset. In stroke 24, local 120 ms gives .56949 versus raw
.62089; smooth One Euro gives .63181 and a 10 DIP endpoint offset. These examples
illustrate why variation alone must not choose a preset across mixed shapes.

## Next physical test

Use the new app to draw diagonals slowly/normally/quickly, then tiny loops,
writing, V/W corners and light-pressure endings. All candidates can be compared
afterward from the same raw recording; no need to redraw each line for analysis.
Separately compare the live feel of candidates 2, 3 and 4, noting revision,
lag and endings. Save notes with the recording. G separates paths and D makes
small differences visible, but D's exaggerated geometry is not actual output.

We have evidence that the longer local window improves this metric on these
diagonals. We do not have evidence of perfect correction, preserved intent in
every stroke, a calibrated hardware noise model, or physical pen-to-photon
latency. Production integration should follow real-device acceptance and
performance work, not merely the lowest offline variation score.
