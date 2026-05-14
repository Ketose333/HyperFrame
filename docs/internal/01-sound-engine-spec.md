# Sound Engine Spec

## Engine Shape

The instrument should keep source modes under one motion-table
performance model:

- Raw: imported PCM or bytebeat stream playback using stored source rate and
  internal pitch metadata. One-shot streams stop at the end; looped imports use
  stored loop start/end points until note release. It does not use wavetable
  `Length`, frame, phase, or interpolation controls.
- Draw: editable free wavetable playback, including short optimized cycles,
  sample-imported looped sources, variable length, higher bit depth,
  interpolation, and the shared 16-frame motion/phase performance system.
- Wave: LSDJ/Game Boy WAV-channel playback over a 16-frame, 32-sample,
  4-bit wave bank with frame and phase motion steps. Wave ignores MIDI velocity;
  mono/poly playback is controlled by the shared `Mono` switch.
- WS: WonderSwan-inspired 32-sample/4-bit wave-bank playback using the shared
  motion/editing surface.
- PCE: PC Engine-inspired 32-sample/5-bit wave-bank playback using the shared
  motion/editing surface.
- SCC: Konami SCC-inspired 32-sample/8-bit wave-bank playback using the shared
  motion/editing surface.

Raw and Draw are treated as the same free-source family: Raw preserves long PCM
or bytebeat timing, while Draw exposes the editable single-cycle surface. This
keeps the instrument focused and avoids a UI that feels like a multi-synth
workstation, while letting each source respect its natural length model.

## Draw/Wave Oscillator

### Wave Table

- Internal editable table size: 512 points.
- Draw active length: 8 to 512 points.
- Hardware wave active length: fixed 32 points in Wave, WS, PCE, and SCC modes.
- Default character length: 32 points.
- Default amplitude depth: 4-bit.
- User can draw or load predefined shapes.

### Playback

- Phase runs over the active length.
- Draw can use nearest or linear interpolation.
- Hardware wave modes always use nearest playback to preserve stepped output.
- Phase reset should be available for consistent note attacks.

### Quantization

Amplitude bit depth should be an explicit parameter:

- Minimum: 2-bit.
- Default: 4-bit.
- Maximum: 16-bit.
- Optional future extension: 12-bit or continuous mode.

The bit depth control is separate from waveform length in Draw mode. Hardware
wave profiles clamp playback depth and sample format to their source family:
Wave and WS use 4-bit unsigned-style wave RAM, PCE uses 5-bit unsigned-style
wave RAM, and SCC uses signed 8-bit wave RAM. These modes are source profiles
inside HyperFrame rather than fault-accurate emulators.

## Wave Operations

### Normalize

Normalize should remove DC offset and scale the waveform to a musically useful
range without unexpectedly changing its active length.

### Smooth

Smooth should average neighboring points and then re-quantize if quantization is
enabled.

### Length Change Behavior

Classic drawn-wave instruments often reduce active point count rather than
resampling the whole wave. For this instrument:

- The editable full table remains intact.
- Active length controls playback/read resolution.
- A destructive "commit length" operation can be added later if users want
  flattening behavior.

This preserves user edits while still allowing low-resolution playback.

## Preset Envelope Convention

ADSR is a generic Raw/Draw envelope rather than a Game Boy envelope
register. Envelope times use millisecond/second values with 1 ms parameter
steps. Hardware wave modes bypass ADSR and keep instant gates; amplitude shape
belongs to motion levels in those modes.

Draw defaults should feel like an immediately playable synth voice: a tiny
attack around 2 ms, a short musical decay around 120 ms, sustain below full
scale, and an 80 ms release for click control. One-shot raw audio import uses
an instant attack, full sustain, and a very short release. SF2 Draw presets
should use SoundFont volume-envelope attack/decay/sustain/release when that
metadata exists.

## SSG-Inspired Modes

SSG should appear as oscillator behavior:

- Square.
- Fixed 50% square.
- Noise.
- Tone/noise blend.
- Octave-doubled square.

The current Noise generator uses a deterministic 15-bit LFSR-derived sequence
when writing editable waveform points. These shapes can be generated into the
same table and then edited by the user.

Changing modes from the editor applies a mode profile. Draw keeps the shared
frame, pitch, phase, and level performance controls available while preserving
the free wavetable controls. Wave snaps and renders to LSDJ/Game Boy
WAV-channel limits: 16 frames, 32 samples per frame, 4-bit amplitude, nearest
playback, integer phase, and four output-volume steps. In Wave mode the shared
ADSR controls are bypassed in favor of an instant note gate; amplitude shaping
should come from WAV volume/frame motion steps. The shared `Mono` switch controls
whether notes steal the current voice or use the normal voice pool.

Wave playback also quantizes note pitch to the Game Boy CH3 11-bit period grid
without exposing an extra control. On trigger, playback starts from sample index
1 to match the CH3 wave RAM read order more closely than a generic wavetable
reset.

WS, PCE, and SCC modes share the same fixed-bank behavior but keep the generic
15-step motion level instead of the Game Boy/LSDJ four-step WAV volume. They
also use instant note gating, fixed velocity, nearest playback, and the shared
mono/poly switch. Pitch is snapped to hardware-inspired divider grids without
exposing another control: WS uses a 3.072 MHz, 11-bit divider grid; PCE uses a
3.579545 MHz, 12-bit divider grid; SCC uses the MSX 3.579545 MHz, 12-bit
period grid. Wave data format is also profile-aware: WS/PCE quantize as
unsigned-style DAC steps, while SCC quantizes as signed 8-bit data. These are
VST source profiles rather than register-level emulation.

Hardware wave profiles also apply small hidden character offsets rather than
adding user-facing controls: unsigned hardware profiles carry a tiny DAC bias,
and all hardware profiles apply very shallow deterministic clock jitter after
pitch quantization. These values are intentionally conservative so they add
source color without making the VST feel unstable in a DAW.

Factory presets for these modes should lean on profile-specific motion rather
than extra UI: WS favors thin 4-bit sweep/step-arp movement, PCE favors 5-bit
bell and DDA-like pluck motion, and SCC favors signed 8-bit lead, echo, and
stacked buzz motion.

In Wave mode, `Motion Rate` is interpreted as the table tick rate. Each motion
step advances once per tick, `Motion Steps` acts as the active synth length,
and `Motion Loop` acts as repeat. The UI keeps shared motion names across modes
so equivalent controls remain easy to recognize.

`Motion Clock` chooses how `Motion Rate` is interpreted. `Hz` preserves the
LSDJ-style free tick rate and remains the default. `BPM` converts the same
integer control into a host-tempo snapped note division denominator. The UI
keeps the same numeric notation in both clock modes, so changing `Hz`/`BPM`
changes timing interpretation rather than rewriting the visible value format.
If the host does not report tempo, the plug-in falls back to 120 BPM.

`Slide` shares that clock choice instead of adding another knob. In Hz mode it
is a snapped seconds value. In BPM mode the same numeric step is interpreted as
a host-tempo division. Turning Slide above `Off` engages a legato mono glide
path in Raw, Draw, Wave, WS, PCE, and SCC modes: the destination note changes
pitch without restarting the wave phase, envelope, or Wave table motion.

When `Motion Loop` is enabled in Draw, WS, PCE, or SCC, the looped motion table
uses a shared engine clock instead of restarting from step 1 for every note.
The clock advances while voices are active and holds its last position when the
instrument is silent, so repeated notes can pick up the current frame motion.
Wave mode keeps LSDJ-style per-note table playback, while hardware wave note
triggers still restart the underlying cycle from sample index 1.

Raw streams keep a separate play-start marker in addition to loop start/end.
This lets notes begin from the middle of a source sample while still looping a
different source region later in playback. The marker is stored with raw stream
state and shown on the raw waveform display.

Because HyperFrame is a VST instrument rather than a fault-accurate emulator,
Wave mode avoids destructive quirks such as wave RAM corruption and forced DAC
pops. When Wave is polyphonic, the engine applies internal headroom compensation
so stacked voices remain usable in a DAW while still using the GB-limited
per-voice wave path.

Hardware-aware parameters should display in source-native notation where it
belongs: ADSR times as compact `ms`/`s` values, ADSR sustain as percent, 4-bit
motion levels as `n/15`, LSDJ phase as hex `$00-$1F`, and pitch/motion
offsets as signed integers. This keeps the UI from exposing long decimal
approximations such as `0.733333`.

## Optimized Cycles

Short optimized cycles belong to Draw mode when source material can be reduced
cleanly to a single editable wavetable cycle. The waveform is therefore visible
in the editor, can be redrawn or processed, and is saved as regular plug-in
state.

Optimized cycles are aligned to an upward zero crossing before being written
into the table, reducing attack clicks when the attack time is set to zero.
Audio source import opens an options dialog before committing the file. The
implemented targets are:

- `Use as Raw Stream`: store the selected mono PCM region and metadata in
  plug-in state, switch to Raw mode, and play it as a one-shot source without
  writing wave frames. Raw stream import reads the requested region in chunks up
  to the file/selection maximum. It uses an internal C4 source reference rather
  than exposing a root-key field.
- `Convert to Wavetable`: mix the selected region to mono, DC-center it,
  optionally snap to one upward-zero-crossing cycle, optionally normalize it,
  force Draw mode, and write it as one editable wavetable cycle. It does not
  create or scan 16 frames.
- `Convert to Wave Frames`: resample the selected region across 16 frames of
  32 samples each so motion/frame movement can scan the sample as GB-style
  wave motion.

Source length remains an import-region property rather than a shared wavetable
`Length` knob. The selected region can be long, but Draw/Wave conversion targets
still collapse it into the existing editable cycle or 16-frame result.

Bytebeat import is available from the Source menu as a separate raw-stream path.
The importer evaluates a user expression over `Start tick` plus `Loop ticks`
using the entered `Clock Hz`, renders exactly one tick loop, stores that loop as
raw stream data, enables full-buffer sample looping, and uses hold/no-
interpolation playback so bytebeat tick values are not linearly blended. The
clock stays as the raw stream rate, while the importer treats `Loop ticks` as
the source cycle, derives source pitch from `Clock Hz / Loop ticks`, then folds
the playback root by octaves near C4. The folded root is the key that plays the
original tick clock; the unfolded source root remains stored as metadata. The
alternate frame-capture target switches to Draw mode and writes selected
32-sample moments into the 16 wave frames.

Raw stream looping is user-facing and independent from motion looping. WAV
`smpl` forward loops are read on raw stream import when they fall inside the
chosen import region. The Source menu exposes `Raw Loop...` for note play start,
loop on/off, and start/end source tick editing, and the raw waveform display
shows the active loop range plus the note start marker. Motion loop still controls
command/motion table stepping only.

Raw-to-Draw exact conversion is separate from the analyzed import path. It
copies the loaded raw loop segment, or the whole raw stream when no valid
loop exists, into Draw only when that source segment is 8-512 samples. The copy
uses Draw length equal to the source segment length, signed 16-bit frame data,
and no interpolation. Longer source material must stay in Raw mode or use the
lossy/optimized `Convert to Wavetable` target.

SF2 import is a bank-level Draw conversion path. The importer reads SoundFont
preset/instrument/sample metadata and scores every convertible zone for each
preset. A valid 8-512 point loop segment is preferred as the cycle source; when
no convertible loop exists, a short 8-512 point whole sample can be imported.
Candidate scoring favors central key/root ranges and simple meaningful names
chosen from preset, instrument, or sample metadata, while avoiding technical
names such as pitch/velocity sample labels. The loop sample rate and loop length
also derive source pitch; the importer folds that pitch by octaves near C4 and
applies only the needed static octave offset when a converted Draw preset would
otherwise land in the wrong register. This avoids rereading large SF2 files
during preset browsing. It also reads SoundFont volume-envelope ADSR generator
values when present. It deduplicates identical converted sample data so repeated
SoundFont presets do not clutter the bank list, keeping the best-scored entry.
It intentionally avoids full multisample/keyzone playback.

LSDJ import is a bank-level Wave conversion path. The importer reads
user-provided `.lsdsng`, `.sav`, and `.snt` files, extracts overwritten WAV
synth frame data, and lists unique 16-frame/32-sample/4-bit Wave-mode presets.
LSDJ manual wave instruments are represented as a single fixed Wave motion step
so the selected wave frame is the audible frame without reintroducing a global
frame-start control.
It does not require or bundle LSDJ ROMs or kits, and it does not try to run a
Game Boy emulator inside the plug-in.

## Character Controls

MVP controls:

- `Length`
- `Bits`
- `Interpolation`
- `Normalize`
- `Smooth`
- `Source...` with audio target, region start/length, normalize, and
  zero-crossing options
- `Bank...` with `.hyperframe` preset import/export and SF2 bank import
- `Motion Table`
- `Motion Clock`
- `Motion Rate`
- `Motion Steps`
- `Engine Mode`
- `Output Gain`

Future controls:

- Very small noise floor.
- Per-note wave start offset.
