# Decisions

## 0001: MVP Keeps Sources Mode-Separated

Status: Revised.

The MVP keeps Draw/Wave and Raw behavior in separate modes instead of
stacking them as simultaneous layers. Raw and Draw sit next to each other as
free-source profiles: Raw preserves long PCM or bytebeat playback, while Draw exposes an
editable cycle. Wave uses the strict GB/LSDJ wavetable path, while WS/PCE/SCC
reuse that fixed wave-bank surface with source-specific limits.

All modes share one motion-table performance layer. The meaning of each motion
step is source-aware: Wave and hardware wave profiles interpret frame and phase
motion steps, and Raw uses pitch/level while ignoring frame/phase.

Reason:

- Keeps the instrument understandable.
- Speeds up first sound delivery.
- Avoids a cluttered UI.

## 0002: Drawn-Wave Length And Bit Depth Are Separate

Status: Accepted.

Waveform length controls point/read resolution. Bit depth controls amplitude
quantization. They are separate user-facing parameters.

Reason:

- Drawn-wave length is not the same as Game Boy 4-bit amplitude.
- Separating them gives better sound design control.

## 0003: PC-98 Soundfont Replacement, Not Chip Emulation

Status: Accepted.

The instrument should evoke the wider PC-98 soundfont palette while remaining a
modern plug-in instrument.

Reason:

- The user wants to replace circulating PC-98 soundfonts.
- Exact emulation would increase complexity and reduce immediacy.

## 0004: No Bundled Copyrighted Source Samples

Status: Accepted.

The project will not include copied PC-98 game samples, ROM dumps, or rhythm ROM
material. Any presets or samples must be original.

Reason:

- Avoids rights ambiguity.
- Keeps the product clean for public release.
