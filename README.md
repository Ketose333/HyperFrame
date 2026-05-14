# HyperFrame

## Download

Get the latest VST3 from the [Releases page](https://github.com/Ketose333/HyperFrame/releases).

HyperFrame is a DAW-oriented chip instrument built around a drawable stepped
wavetable oscillator, inspired by the Game Boy wave channel, classic drawn-wave
editing, LSDJ-style wave-table performance, editable reference-cycle waves, and
raw source streams.

This repository currently contains the product/design notes and a small
framework-neutral DSP core prototype. The first implementation target is a VST3
instrument, but the engine is intentionally kept independent from any plug-in
SDK.

## MVP Direction

- Raw mode as the PCM/bytebeat stream source for imported source material.
- Draw mode as the free editable wavetable source.
- Drawn-waveform length control from 8 to 512 points in Draw mode.
- Quantized amplitude depth, with 4-bit as the default character setting and
  up to 16-bit for cleaner reference-cycle material in Draw mode.
- Optional interpolation in Draw mode.
- Wave mode as the LSDJ/Game Boy WAV-channel source: 16 frames, fixed
  32-sample/4-bit playback, nearest output, frame motion steps, 4-step WAV volume,
  fixed note velocity, instant note gating, shared mono/poly control, and
  LSDJ-style phase shaping with Normal, Resync, and Resyn2 behavior. Wave pitch
  is quantized to the Game Boy CH3 period grid, with VST-friendly headroom when
  polyphonic Wave playback is enabled.
- WS, PCE, and SCC modes as hardware-inspired 32-sample wave-bank profiles that
  reuse the same motion/editing surface without adding knobs. They clamp length,
  bit depth, interpolation, velocity, gate, mono defaults, and pitch divider
  grids to source-specific values while avoiding fault-level emulation. WS/PCE
  use unsigned-style wave quantization; SCC uses signed 8-bit wave quantization.
- Normalize and smooth operations.
- Audio import starts with an options dialog. Implemented targets can extract
  a single Draw wavetable cycle, explicitly pack a region into 16 GB-style wave
  frames, or keep the source as a raw stream. Only the frame-oriented target
  writes 16-frame motion. Audio import reads the selected region up to the
  file/selection maximum in chunks, so long raw streams are not squeezed through
  the wavetable `Length` control.
- Raw and Draw are the same free-source family at the UI level: Raw keeps long
  PCM or bytebeat streams intact, while Draw keeps an editable cycle. Raw reuses
  existing ADSR, gain, mono/poly, and motion pitch/level controls instead of
  adding source-length knobs. A loaded raw stream can be copied exactly into Draw
  when the active loop, or the whole stream when no loop is present, is 8-512
  samples; longer material stays in Raw mode or uses the analyzed wavetable
  conversion. Raw streams can also start note playback from a user-selected
  source sample without changing the loop range. Raw playback can either follow
  the key gate as before or ignore note-off and continue until the source ends;
  the shared Bits control applies non-destructive signed output quantization to
  Raw without rewriting the loaded stream.
- ADSR envelope for Draw and Raw. Hardware wave modes use an instant
  note gate; amplitude shaping belongs to frame/volume motion steps.
- Slide is exposed as a single shared performance control. It gives legato mono
  glide between notes in Raw, Draw, and hardware wave modes without
  restarting phase, envelope, or Wave table motion. Turning Slide above `Off`
  makes the voice path monophonic for glide even when the shared `Mono` switch
  is off. When `Motion Clock` is set to BPM, the same Slide control snaps to
  host-tempo note divisions instead of adding a separate sync knob.
  Frame start plus Wave step bend, vibrato-rate, and loop-start controls live
  behind compact source-menu dialogs so they remain available without expanding
  the main surface.
- Preset envelope convention: hardware wave modes use instant gates; Draw
  defaults use a tiny attack and short release for click control, with longer
  releases reserved for intentionally sustained keys, pads, phrases, or tails.
- Current-state preset import/export using `.hyperframe` files. The file stores
  parameter state, all wave frames, motion steps, and raw stream data when a
  raw source is loaded; the format may still change before a 1.0 release.
- SF2 bank import converts only SoundFont presets with a valid 8-512 sample loop
  or short whole sample into HyperFrame Draw presets. The importer scores all
  convertible instrument/sample zones for each preset, preferring loop cycles,
  central key/root ranges, and simple meaningful names from the SoundFont preset,
  instrument, or sample metadata. Imported bank entries keep the small converted
  cycle in memory, then copy it sample-for-sample into signed 16-bit Draw data
  when selected. SoundFont volume-envelope ADSR is used when present. Duplicate
  converted cycle data is shown only once, keeping the best-scored entry. Full
  multisample/keyzone playback is intentionally out of scope.
- LSDJ bank import reads user-provided `.lsdsng`, `.sav`, and `.snt` files and
  converts WAV synth frame data into Wave-mode preset banks. ROMs and kits are
  not bundled or required.
- Square/noise wave generation as editable oscillator sources.
- A shared phase and motion-table layer: Draw can use the same 16-frame,
  pitch, phase, and level motion performance model without inheriting GB
  wave-channel limits; Wave applies the strict LSDJ/GB WAV-channel limits;
  WS/PCE/SCC apply fixed 32-sample hardware-inspired wave-bank profiles.
  Motion timing can run as an LSDJ-style Hz tick rate or snap to host-BPM note
  divisions for DAW arrangement work, while the UI keeps the same numeric
  notation across both clock modes.
  Short optimized cycles live in Draw when they can be reduced cleanly, so they
  remain visible and editable instead of becoming a hidden sample-player layer.

## Project Notes

Internal planning documents live in `docs/internal`.

## Build

Core smoke test:

```powershell
.\scripts\build_core.ps1
```

VST3 wrapper:

```powershell
.\scripts\build_vst3.ps1
```

Optional vendor metadata can be set at configure time:

```powershell
cmake -S . -B build-juce -G "Visual Studio 17 2022" -A x64 -DBUILD_JUCE_PLUGIN=ON -DHYPERFRAME_COMPANY_NAME="Your Vendor"
```

The first VST3 wrapper uses JUCE and is enabled through `BUILD_JUCE_PLUGIN`.
The DSP core remains independent from JUCE so other wrappers can be added later.

Release package:

```powershell
.\scripts\package_release.ps1
```

## License

HyperFrame is licensed under the GNU Affero General Public License v3.0 only
(`AGPL-3.0-only`). See `LICENSE`.

The VST3 wrapper uses JUCE 8.0.12 under JUCE's AGPLv3 option. Third-party
notices are listed in `THIRD_PARTY_NOTICES.md`.

## Release Checklist

- Confirm the release is distributed under `AGPL-3.0-only`.
- Confirm release metadata: `PROJECT_VERSION` is currently `0.1.0`, and
  `HYPERFRAME_COMPANY_NAME` defaults to `HyperFrame` unless overridden at configure
  time.
- Include `LICENSE` and `THIRD_PARTY_NOTICES.md` with binary packages.
- Run a DAW smoke test: scan VST3, play MIDI, change presets, import an SF2 Draw
  bank, import an LSDJ Wave bank, export/import `.hyperframe`, save the project,
  and reopen it.
