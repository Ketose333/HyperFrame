# Implementation Plan

## Phase 1: Core

- Add framework-neutral wavetable oscillator.
- Add quantization, length, interpolation, normalize, and smooth operations.
- Add ADSR.
- Add a small offline render/test harness.

## Phase 2: Plugin

- Choose wrapper stack.
- Implement VST3 instrument shell.
- Register stable parameters.
- Wire MIDI note events to the DSP core.
- Implement state save/load.

## Phase 3: UI

- Draw waveform editor.
- Add shape buttons.
- Add length, bits, interpolation, normalize, smooth, phase, motion table, engine mode, and envelope controls.
- Add preset browser.

## Phase 4: Content

- Build original factory presets.
- Tune gain staging.
- Validate behavior in at least two DAWs.

## Phase 5: Expansion

- Add a reference-grounded operator or noise model only after its hardware
  behavior is specified.
- Add CLAP if the VST3 build is stable.
- Add standalone app if useful for preset editing.
