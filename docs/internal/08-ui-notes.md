# UI Notes

## First Screen

The first screen should be the instrument itself, not a marketing panel.

Recommended layout:

- Large waveform editor across the top half.
- Shape/action buttons near the waveform.
- Length, bits, interpolation, normalize, and smooth controls immediately beside
  or below the waveform.
- Envelope controls in a compact strip.
- Preset browser in a narrow top or side area.
- Preset import/export should live in the existing preset row. Do not add more
  vertical rows for preset file management; the editor must stay usable on
  short laptop displays.
- File actions should share a remembered chooser directory so preset, sample,
  and SF2 workflows feel like one import/export surface instead of unrelated
  browser paths.

## Interaction Priorities

- Drawing the waveform must feel immediate.
- Length and bit controls should visibly update the waveform.
- Interpolation should be obvious in sound and optionally reflected visually.
- Normalize and smooth should be action buttons, not continuous knobs.
- Top action menus should have distinct responsibilities: `Bank...` for
  `.hyperframe` preset files plus SF2/LSDJ bank imports, `Source...` for current
  audio-source import/conversion plus compact playback-start and advanced step
  dialogs, and `Wave RAM...` for GB/Wave RAM hex actions.

## Visual Direction

The UI should suggest old Japanese computer audio without becoming a fake
hardware panel.

Useful cues:

- Pixel-grid waveform.
- Crisp typography.
- High-contrast editable graph.
- Compact controls.
- Restrained color accents.

Avoid:

- Overly literal hardware screws, fake wood, or huge decorative panels.
- Register-level chip UI for the MVP.
- A massive multi-layer mixer before the one-oscillator instrument is proven.
