# Preset Guidelines

## Goal

Presets should replace the musical role of common PC-98 soundfonts, not recreate
specific commercial game sounds.

## Categories

- Bass.
- Lead.
- Bell.
- Mallet.
- Organ.
- Brass.
- String-like.
- Pad.
- Noise percussion.
- FX.

## Naming

Use descriptive, original names. Avoid names that imply a direct copy of a game,
composer, hardware board, or copyrighted soundtrack.

Good examples:

- Glassy 4-Bit Bell.
- Narrow Square Lead.
- Dusty Menu Organ.
- Stepped Brass Stack.
- Neon Mallet.

Avoid:

- Names of specific games.
- Names of specific commercial patches.
- Names implying official PC-98 hardware branding.

## Sound Design Bias

- Strong attacks.
- Short-to-medium decays.
- Clear looped tones.
- Useful velocity response.
- Optional vibrato on mod wheel.
- Minimal effects inside the preset.
- The first preset must be `Init`.
- `Init` should stay neutral: Draw mode, sine shape, 32-point length, 4-bit
  depth, no motion table, no phase shaping, 0 ms attack, 31 ms release,
  and 50% output gain.
- Factory presets should be ordered by source family: Init, Draw, Wave, WS,
  PCE, then SCC. Imported SF2 Draw banks and LSDJ Wave banks appear after
  factory presets.
- A Wave preset may approximate GB pulse duty by storing fixed 32-sample,
  4-bit duty frames, but it should not claim to emulate CH1/CH2 sweep,
  envelope, or length-timer behavior unless those systems exist in the engine.
- WS/PCE/SCC presets should make their profile identity obvious through frame,
  pitch, phase, and level motion before adding any new controls. Prefer
  stepped sweep/arp shapes for WS, 5-bit bell/DDA-style plucks for PCE, and
  signed 8-bit lead/echo/stack motion for SCC.
- Hardware wave presets use instant gates. Draw presets should use
  envelope values that fit the patch role, with tiny attacks and short releases
  only where they prevent clicks.
- SF2 import is a bank conversion path, not a full SoundFont player. Each SF2
  preset should become one Draw preset when a scored instrument/sample zone has
  a loop segment or short whole sample that fits in 8-512 Draw points. Prefer
  valid loop cycles, central key/root ranges, and simple meaningful names from
  the SoundFont metadata. Non-convertible presets should be omitted from the
  imported bank list.
- LSDJ import is a bank conversion path, not an embedded LSDJ session. Import
  only user-provided `.lsdsng`, `.sav`, and `.snt` WAV synth frame data as
  Wave presets; do not bundle ROMs, kits, or song playback behavior.

## Expansion Preset Targets

- 8 basses.
- 12 leads.
- 8 bells/mallets.
- 6 organs.
- 6 brass/string-like sounds.
- 6 noise/FX sounds.

Longer-term target: 46 presets. The v0.1 factory bank may ship leaner when the
import paths cover broader source material.
