# HyperFrame v0.1.0 Release Notes

## Summary

HyperFrame v0.1.0 is a JUCE-based VST3 synth focused on editable 8/16-bit-style
wavetable sources, hardware-inspired wave profiles, and import-driven preset
banks.

## Highlights

- Raw, Draw, Wave, WonderSwan, PC Engine, and SCC engine modes.
- Raw mode supports gated note-off playback or full-source playback, plus
  non-destructive signed output bit-depth control.
- Draw mode supports editable 8-512 point waves, 2-16 bit quantization, optional
  interpolation, ADSR, motion, phase, and glide.
- Wave mode uses an LSDJ/Game Boy WAV-channel-inspired profile with 16 frames,
  fixed 32-sample 4-bit wave RAM, nearest playback, instant gate, CH3-style pitch
  quantization, LSDJ phase modes, and 4-step WAV volume.
- WS/PCE/SCC modes provide fixed 32-sample hardware-inspired wave-bank profiles
  with source-specific bit depth, sample format, pitch grid, instant gates, and
  motion.
- SF2 import converts eligible 8-512 sample loops into Draw preset-bank entries
  and omits duplicate converted cycles.
- LSDJ import converts user-provided `.lsdsng`, `.sav`, and `.snt` WAV synth
  frame data into Wave preset-bank entries.
- `.hyperframe` preset export/import is available for plug-in state exchange.
- Slide supports legato mono glide across Raw, Draw, Wave, WonderSwan, PC Engine, and SCC,
  with Hz/BPM-synced display behavior.

## Release Metadata

- Product name: HyperFrame
- Vendor: HyperFrame
- Version: 0.1.0
- Format: VST3
- Licence: AGPL-3.0-only
- JUCE: 8.0.12
- liblsdj commit: 6023c4e48ad8280abacfddba60f2689e2442d79c

## Packaging Notes

- Include `LICENSE` and `THIRD_PARTY_NOTICES.md` in binary packages.
- Do not bundle copyrighted ROMs, game samples, commercial soundfonts, or LSDJ
  song/save files.

## Verification

- Core build and core smoke test passed.
- VST3 build passed.
- DAW smoke test was completed manually before release packaging.
