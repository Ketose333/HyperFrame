# Architecture

## Modules

### DSP Core

Pure C++ audio code with no plug-in SDK dependencies.

Responsibilities:

- Voice allocation.
- MIDI note handling.
- Oscillator phase and waveform playback.
- Envelope generation.
- Parameter smoothing where needed.
- Audio rendering.

### Wave Model

Stores the editable waveform and exposes non-realtime editing operations:

- Draw point.
- Load generated shape.
- Smooth.
- Normalize.
- Quantize.
- Set active length.

Realtime rendering reads an immutable or lock-free snapshot of this data.

### Plugin Wrapper

Owns host-facing concerns:

- VST3 parameter registration.
- MIDI/event translation.
- Preset serialization.
- Audio bus configuration.
- State save/load.

### UI

Owns user-facing editing:

- Wave display.
- Drawing.
- Shape buttons.
- Length and bit controls.
- Envelope controls.
- Preset browser.

UI must never mutate audio-thread data directly.

## Audio Thread Rules

- No allocation.
- No locks.
- No file IO.
- No logging.
- No waveform smoothing/normalizing on the audio thread.

Waveform edits should produce a new prepared table, then swap it atomically or
through a small lock-free handoff.

## First Data Flow

1. UI edits `WaveModel`.
2. `WaveModel` prepares a render table.
3. Plugin wrapper hands the table snapshot to the DSP core.
4. Voices render from that table using current parameters.

## Open Decisions

- JUCE vs direct VST3 SDK for first wrapper.
- Whether to ship CLAP in parallel.
- Whether preset files are JSON, binary, or host-state only.
- How much visual styling to build before the sound is proven.

