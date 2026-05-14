# Parameter Map

## MVP Parameters

| ID | Name | Range | Default | Automatable | Notes |
| --- | --- | --- | --- | --- | --- |
| `wave_length` | Wave Length | 8-512 | 32 | Yes | Active read length in Draw. Hardware wave modes render as fixed 32 samples. |
| `wave_bits` | Wave Bits | 2-16 | 4 | Yes | Amplitude quantization in Draw. Hardware wave modes clamp to their profile depth and sample format. |
| `interpolation` | Interpolation | Off/On | Off | Yes | Nearest vs linear playback in Draw. Hardware wave modes render nearest. |
| `attack` | Attack | 0-5s, 1ms steps | 2ms | Yes | Generic Raw/Draw ADSR attack. Ignored in hardware wave modes. |
| `decay` | Decay | 0-5s, 1ms steps | 120ms | Yes | Generic Raw/Draw ADSR decay. Ignored in hardware wave modes. |
| `sustain` | Sustain | 0-100%, 1% steps | 85% | Yes | Generic Raw/Draw ADSR sustain level. Ignored in hardware wave modes. |
| `release` | Release | 0-10s, 1ms steps | 80ms | Yes | Generic Raw/Draw ADSR release. Ignored in hardware wave modes. |
| `lsdj_phase` | Phase | 0-31, integer | 0 | Yes | Draw/Wave phase shaping amount. |
| `lsdj_phase_mode` | Phase Mode | Normal/Resync/Resyn2 | Normal | Yes | Draw/Wave phase behavior. |
| `slide_time` | Slide | 0-2s, snapped steps | 0ms | Yes | Legato mono glide between notes across Raw, Draw, Wave, WS, PCE, and SCC. Turning it above `Off` makes the voice path monophonic for glide even when `Mono` is off. BPM interprets the same step as a host-tempo division. |
| `selected_frame` | Frame | 1-16 | 1 | Yes | Editable wave frame. |
| `motion_table` | Motion Table | Off/On | Off | Yes | Step-motion playback. |
| `motion_loop` | Motion Loop | Off/On | Off | Yes | Repeat motion table. Draw/WS/PCE/SCC use a shared engine clock; Wave restarts per note to match LSDJ-style instrument playback. |
| `motion_clock_mode` | Motion Clock | Hz/BPM | Hz | Yes | Interpret motion timing as a free Hz tick rate or host-BPM snapped musical division. |
| `motion_rate` | Motion Rate | 1-64, integer | 12 | Yes | In Hz mode, motion advance rate in Hz. In BPM mode, the same numeric value becomes a host-tempo division denominator and snaps to power-of-two steps. The UI notation stays numeric in both modes. |
| `motion_steps` | Motion Steps | 1-16 | 8 | Yes | Active motion step count. In Wave mode this is the active synth length. |
| `motion_loop_start` | Motion Loop Start | 0-15 | 0 | Yes | Repeat point for motion-table looping. Exposed in `Step Advanced...` with 1-based step labels. |
| `engine_mode` | Engine Mode | Raw/Draw/Wave/WonderSwan/PC Engine/SCC | Draw | Yes | Raw plays imported PCM or bytebeat streams; Draw is the free editable wavetable source; Wave is the LSDJ/Game Boy WAV-channel source; WonderSwan/PC Engine/SCC are fixed hardware-inspired wave-bank profiles. |
| `mono_mode` | Mono | Off/On | Off | Yes | Shared mono/poly switch. When on, a new note steals the current voice. Hardware wave modes ignore MIDI velocity. |
| `motion_step_##_frame` | Step Frame | Hold/1-16 | Hold | Yes | Draw/Wave frame motion. |
| `motion_step_##_pitch` | Step Pitch | -24 to +24 semitones, integer | 0 | Yes | Step pitch offset. |
| `motion_step_##_bend` | Step Bend | -96 to +96 semitones/sec | 0 | Yes | Wave step continuous pitch movement, exposed in `Step Advanced...`. |
| `motion_step_##_phase` | Step Phase | -31 to +31, integer | 0 | Yes | Draw/Wave phase offset. |
| `motion_step_##_vib_rate` | Step Vibrato Rate | 0-64 Hz | 0 | Yes | Wave step vibrato speed, exposed in `Step Advanced...`; Wave mode uses the visible phase/depth lane as vibrato depth. |
| `motion_step_##_level` | Step Level | 0-15/15 | 15/15 | Yes | Step output level. Wave mode snaps playback to LSDJ WAV volume steps: 0%, 25%, 50%, 100%. |
| `gain` | Gain | 0-150% | 50% | Yes | Output trim. |

## Mode Profiles

Changing `engine_mode` from the editor applies a source-aware profile:

- Raw plays the imported PCM or bytebeat stream as a one-shot or looped source
  using stored source-rate and internal pitch metadata. It reuses ADSR, gain,
  mono/poly, motion pitch, and
  motion level, while wavetable length, bit depth, interpolation, frame, and
  phase controls are disabled so long samples do not become knob-driven data.
  `Raw Loop...` stores an independent note play-start sample alongside loop
  start/end, so a source can begin from the middle and still loop elsewhere.
  Raw and Draw are intentionally adjacent free-source profiles rather than
  unrelated synth engines.
- Draw keeps the shared frame, pitch, phase, and level motion performance
  controls available while preserving free wavetable length, bit depth, and
  interpolation.
- Wave snaps and renders to LSDJ/Game Boy WAV-channel constraints: 16 frames,
  32 samples per frame, 4-bit depth, nearest playback, integer phase/pitch
  motion steps, 4-step WAV volume, fixed note velocity, and an instant note gate
  instead of ADSR shaping. Mono/poly remains controlled by the shared `Mono`
  switch. Wave pitch is rendered on the Game Boy CH3 period grid, and note
  trigger starts from wave RAM sample index 1. Wave mode transition and Wave
  imports automatically restore 32/4/nearest/Mono defaults without adding
  another control. If host state, automation, or shared controls push Wave
  parameters away from these limits, the plug-in writes strict Wave values back
  to the parameters: length 32, 4-bit, nearest interpolation, integer motion
  rate, and LSDJ WAV-volume motion levels.
- WS, PCE, and SCC are fixed hardware-inspired wave-bank profiles. They reuse
  the same frame/motion editor, clamp to 32 samples with 4-bit, 5-bit, and 8-bit
  depth respectively, use nearest playback, fixed velocity, instant gate, and
  Mono defaults, snap pitch to hardware-inspired divider grids, and keep generic
  15-step motion levels. WS/PCE use unsigned-style wave quantization; SCC uses
  signed 8-bit wave quantization. They do not add new knobs and are not
  register-level emulators.
## Value Notation

Compact value notation avoids long decimal displays:

- ADSR time uses compact `ms`/`s` values such as `125ms` or `1.69s`.
- ADSR sustain uses percent values such as `73%`.
- 4-bit amplitude uses fractions: `11/15` instead of `0.733333`; Wave motion
  levels render as the closest LSDJ WAV volume step.
- LSDJ phase uses hex: `$15` instead of `21`.
- Motion rate, slide, pitch, and signed motion offsets use compact text such as
  `12`, `16`, `+7st`, or `-3`.

## Non-Automated Actions

- Draw waveform point.
- Generate sine.
- Generate triangle.
- Generate saw.
- Generate square.
- Generate noise.
- Normalize.
- Smooth.
- Import audio with an options dialog. Implemented targets are `Use as Raw
  Stream`, `Convert to Wavetable`, and `Convert to Wave Frames`; region
  start/length, normalize, and zero-crossing options are available in the
  dialog. Only `Convert to Wave Frames` writes 16-frame motion; `Convert to
  Wavetable` writes one Draw cycle, and `Use as Raw Stream` stores PCM data and
  switches to Raw mode.
- Import bytebeat from the `Source...` menu. The dialog takes an expression,
  `Clock Hz`, `Start tick`, and `Loop ticks`. `Use as Raw Stream` renders one
  tick loop, stores it as raw stream data, enables full-buffer sample looping,
  and uses hold/no-interpolation playback. The clock remains the raw stream
  rate, `Loop ticks` is treated as the source cycle, and HyperFrame derives
  source pitch from `Clock Hz / Loop ticks`. The playback root is folded by
  octaves near C4; that folded key plays the original tick clock while the
  unfolded source root is stored as metadata. `Capture 16 Frames` switches to
  Draw mode and captures selected moments into the frame bank.
- Edit raw stream play start and loop points from `Source... > Raw Loop...`.
  This is separate from motion looping: raw play/loop controls raw playback
  position, while motion loop controls command/table stepping. The waveform
  display marks the note start and active raw loop range. WAV `smpl` forward
  loops are imported when they fit inside the chosen audio region.
- Exact current Raw-to-Draw conversion lives in the `Source...` menu. It copies a
  loaded raw loop segment, or the whole raw stream when no loop is present, only
  if the source segment is 8-512 samples. It uses signed 16-bit Draw data and
  leaves longer material in Raw mode.
- Import/export current preset as a `.hyperframe` file. This uses the plug-in
  state blob directly during the pre-release phase, so parameter values, wave
  frames, motion steps, selected motion step, and raw stream data travel
  together without a long-lived compatibility promise.
- Import SF2 bank. The importer follows SF2 preset/instrument/sample metadata,
  scores every convertible instrument/sample zone per preset, and lists entries
  whose loop segment or short whole sample can fit in 8-512 Draw points. Valid
  loop cycles are preferred, then central key/root ranges and simple meaningful
  names from preset, instrument, or sample metadata. Those entries append
  lightweight converted sources after the factory preset list as Draw presets.
  The cached source is copied into signed 16-bit Draw when a bank preset is
  selected. Converted loops use the same source-pitch octave folding process and
  apply a static octave offset only when needed. SoundFont volume-envelope ADSR
  is preserved where present. Duplicate converted sample data is listed only
  once, keeping the best-scored entry.
- Import LSDJ bank. The importer reads `.lsdsng`, `.sav`, and `.snt` files and
  appends unique overwritten WAV synth frame sets after the factory preset list
  as Wave presets. Imported entries use the Wave profile: 32 samples, 4-bit
  unsigned wave RAM, nearest playback, instant gate, mono, and LSDJ 4-step
  motion level.
- Copy/paste waveform.

## Preset Authoring Notes

- Hardware wave presets use instant gates because ADSR is bypassed in those
  modes.
- Draw defaults use a tiny attack and short release for click control.
- SF2 Draw presets should use SoundFont volume-envelope ADSR values when
  present.
- ADSR envelope times use 1 ms steps.
- Use longer release values only for intentionally sustained keys, pads,
  phrases, or tails.
- Reset waveform.

## Deferred Parameters

- User-selectable noise/LFSR profiles beyond the current deterministic Noise
  generator.
- SSG tone/noise blend.
- User-facing DAC bias and clock jitter controls. Small hidden profile values
  are already applied in hardware wave modes.
- Mod wheel vibrato depth.

## Host State

The plug-in state should store:

- All parameter values.
- Full 512-point editable waveform for each frame.
- Active length.
- Bit depth.
- Draw loop on/off plus loop start/end for imported Draw sources that preserve
  embedded loop points.
- Raw stream data and internal pitch metadata when Raw mode content is present.
  Audio imports use C4 internally; bytebeat raw streams store both the unfolded
  source root and an octave-folded playback root.
- Raw stream loop on/off plus loop start/end. Bytebeat raw streams enable a
  full-buffer loop by default; users can later edit loop points from the Source
  menu.
- Preset metadata, if applicable.
