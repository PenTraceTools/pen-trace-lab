# Independent pen tracking and correction layers

Research/design note, 2026-09-12. This is not a specification for a validated
hardware product or a recommendation to attach magnets to the screen.

## Is a tracking overlay physically possible?

Yes, an independent position-sensing system is possible in principle. But a
passive magnetic film is not a position recorder: measurement needs sensors,
electronics, a compatible signal source and calibration. An electromagnetic
digitizer is a complete sensing system, not simply a screen protector.

For example, Wacom documents EMR systems with a sensor beneath the display that
generates a field and communicates with a compatible pen. This does not establish
compatibility with an arbitrary Microsoft/MPP/AES pen or an existing touchscreen.
[Wacom's technology description](https://wacom.com/en-us/about-wacom/technologies).

Adding a second sensing layer would require validation of interference, display
materials, sensor-to-tip distance, pressure/contact behavior, alignment and the
pen protocol. These are engineering concerns, not a demonstrated failure on this
user's device. Wacom's own account of adapting EMR to an OLED display illustrates
that display/sensor integration can introduce interference requiring engineering
work. [Wacom's OLED/EMR development account](https://community.wacom.com/en-us/wacom-movink-13-oled-display/).

I have not established an off-the-shelf transparent magnetic overlay that can
independently measure this user's existing pen more accurately than its built-in
digitizer, or universally correct its wobble. The precise device/pen model is
still needed before investigating a compatible product.

## Better first reference measurements

1. A controlled guide or precision motion stage constrains the pen to a known path.
   If accurately constructed and independently verified, this reduces uncertainty
   from the user's hand without adding another screen sensor. Microsoft documents
   PT3 fixtures and pen holders for hardware validation, including moving and
   stationary jitter tests. This is real test equipment, not a Windows repair mode.
   [Microsoft hardware requirements](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/hardware-requirements-and-vendor-information),
   [PT3 guidance](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/how-to-use-the-precision-touch-testing-tool).
2. A calibrated optical setup could independently observe actual pen-tip movement.
   This is a proposed experiment, not functionality implemented in this app.
   It needs enough spatial resolution for the wobble amplitude, sufficient frame
   rate, synchronized timestamps, calibrated screen geometry and control of lens
   distortion, parallax and hand/pen occlusion. Tracking a marker on the barrel
   is not equivalent to tracking the tip unless orientation and tip offset are
   accounted for. A casual phone recording is useful visually but is not
   automatically a submillimeter ground-truth measurement.

For quantitative work, reference-system uncertainty must be appreciably smaller
than the error being measured. A motor command alone is not proof that the stage
or flexible pen tip followed that exact path; measure repeatability/backlash and
reference positioning too. Do not infer hardware accuracy from an on-screen guide.

## Could it become a correction layer?

Potentially, but that is a separate research project:

- With a genuinely more accurate external tracker, the app could use its position
  or fuse it with pen pressure/tilt. Synchronization, coordinate transforms,
  latency, dropout handling and confidence would need validation. It would be a
  new input system, not a passive fix applied to the existing screen.
- Repeated reference trials could reveal stable spatial bias. A calibrated
  correction map might compensate that repeatable component. Validate it on
  held-out positions, directions, speeds, pen angles and curves; retain the
  uncorrected samples and make correction reversible.
- Random error, changing interference and genuine intentional details cannot
  reliably be separated by a fixed map. Device/firmware updates or a different
  pen could invalidate calibration. Avoid generalizing one ruler trace to all
  freehand strokes.

The next practical step remains baseline recordings and a repeatable guide/stage,
not building an unvalidated magnetic overlay. No hardware modification, sensor
fusion or correction map is implemented in Pen Trace Lab at this stage.

## Electrical signals versus Windows reports

Additional HID properties exposed through `GetRawPointerDeviceData` are still
digital reports supplied by the device, not guaranteed access to electrode or
magnetic sensor waveforms. Internal-signal access can be manufacturer-specific.
See [DESIGN.md](DESIGN.md) for the documented API boundary and sources.

## What the app comparison means

- Solid blue: Windows-reported pen positions after documented coordinate mapping,
  connected without our positional filter.
- Dashed orange: the selected algorithm applied to those same positions/timestamps.
- Optional purple: a curve-interpolation experiment, not an exact InfiniPaint
  renderer reproduction.

Both comparison paths appear when a non-Off filter is selected and both View
layers are enabled. With Off, the paths coincide and are drawn once. Dash gaps are
only a rendering style: they do not remove points from filtering or measurement.
Neither displayed path is an independent measurement of the true physical tip.
