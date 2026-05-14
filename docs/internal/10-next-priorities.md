# Next Priorities

## Source Import Finish

The main audio/bytebeat import path is now in place. Source import has:

- `Import Audio...` with `Use as Raw Stream`, `Convert to Wavetable`,
  and `Convert to Wave Frames` targets.
- `Import Bytebeat...` with expression, clock Hz, start tick, and loop ticks.
  Raw bytebeat import renders one tick loop, enables full-buffer sample looping,
  uses hold/no-interpolation playback, estimates a source cycle, and folds the
  internal root by octaves near C4 so C keys stay on C pitch classes without
  resampling or large playback jumps.
- `Raw Loop...` for user-editable raw stream play-start plus loop on/off and
  start/end source ticks. WAV `smpl` loops are imported when available. The raw
  waveform display shows both the active sample loop range and note start marker.
- `Step Advanced...` source-menu dialog for editing Wave bend/vibrato/loop
  start values without expanding the main control grid.
- `Exact Raw to Draw` for copying valid 8-512 raw loop segments into
  Draw without interpolation.

Remaining implementation order:

1. Add optional zero-crossing assistance for manually edited raw loop points.
2. Add direct draggable raw-loop markers in the waveform display.
3. Add NES DPCM conversion/playback with pitch table and 1-bit delta handling.
4. Investigate SPC700/BRR block decode, filter behavior, loop points, and
   envelope/GAIN interaction before implementing BRR import.

## Current State

The plug-in now has a custom JUCE editor with a stepped waveform display and
organized sections for:

- `SOURCE`: engine mode, wave length, bit depth, frame, interpolation.
- `VOICE`: phase mode, gain, phase amount, slide, mono.
- `MOTION`: clock mode, rate, steps, motion on/off, motion loop.
- `STEP`: frame, pitch/bend, phase/vibrato depth, level/volume.
- `ENVELOPE`: ADSR.
- `KEYS`: MIDI keyboard.

The display renders the active wave length from the internal 512-point table in
waveform modes, and renders raw stream amplitude buckets plus raw-loop markers
in Raw mode.

## Recommended Next Priority

### 1. Direct Raw Loop Dragging

The numeric `Raw Loop...` dialog is functional, but the displayed loop
markers should become draggable handles for faster auditioning.

### 2. Bytebeat Authoring Comfort

Keep the current rendered-loop sound model, but add convenience around common
clock/loop tick values, expression recall, and clearer validation messages.

### 3. Parameter And Audio Thread Cleanup

Source snapshots already keep waveform/raw-sample edits off the audio-thread
lock path. Continue tightening parameter handoff and editor updates as the UI
adds more source editing surfaces.

Minimum behavior:

- Keep waveform editing off the audio thread.
- Avoid direct UI mutation of audio-thread data.
- Add a prepared render snapshot if edits become glitchy.

## Suggested Order

1. Add drawable waveform editing.
2. Add waveform state serialization.
3. Add shape buttons.
4. Add original starter presets.
5. Add grit/downsample character controls.
