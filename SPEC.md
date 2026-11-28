# zyntuner: specification

Living document: each decision made in the chat is written here before it's implemented.
Status of each section: TODO / AGREED / DONE (implemented and tested on a zynthian).

## Identity — AGREED
- Name: zyntuner · symbol: `zyntuner` · URI: `urn:riban:tuner`
- Maker: riban · license: GPL-3.0-or-later · LV2 category: lv2:AnalyserPlugin
- Purpose: Low CPU tuner with soft mute and microtonal scales, and spectrum analyser (0.4)

## I/O — AGREED
- Audio: 2 in, 2 out
- MIDI: no input, no output

## Controls (LV2 input ports) — DONE (0.4 tested on zynthian-v5)
Pages = port groups, 4 controls per page in zynthian. Symbols are stable API.

| Page | Symbol | Name | Range | Default | Unit | Type | Designation / notes |
|---|---|---|---|---|---|---|---|
| Tuner | `reference` | A4 reference | 415 .. 466 | 440 | Hz | float |  |
| Tuner | `mode` | Mode | 0 .. 2 | 0 (Tuner) |  | enum | Tuner / Spectrum / Both. Since 0.4, replaces `threshold` (removed, fixed at -60 dB) |
| Tuner | `bypass` | Bypass | 0 .. 1 | 1 (bypassed, audio passing) |  | boolean | bypass (LV2 `lv2_enabled`, default 0 set by `port_extras.json`) |
| Tuner | `scale` | Scale | 0 .. 17 | 0 (12-TET) |  | enum | from `zyngui/zyntuner_scales.py`, new scales appended only |

## Outputs (LV2 output ports → widget monitors, ~30 Hz) — DONE

| Symbol | Name | Range | Unit | Meaning / how it's computed |
|---|---|---|---|---|
| `note` | MIDI note | 0 .. 127 |  | nearest MIDI note; keeps its last value when not valid |
| `note` | (see above) | | | since 0.2: nearest 12-TET MIDI note of the target scale degree |
| `cents` | Cents | -150 .. 150 |  | deviation from the nearest degree of the selected scale |
| `valid` | Pitch valid | 0 .. 1 |  |  1 = pitch detected, 0 = silence / no pitch / bypassed |
| `degree` | Scale degree | 0 .. 63 |  | nearest degree (0 = root); keeps its last value when not valid |
| `octave` | Octave | -1 .. 10 |  | octave (tritave for Bohlen-Pierce) of `degree`, 4 = the root's at C4 |
| `band_00` .. `band_31` | 22 Hz .. 18.0 kHz | -80 .. 0 | dB | spectrum band levels (0.4); -80 when not in Spectrum mode or bypassed |

## DSP — DONE
- Modes (0.4): only the selected analysis runs, and none while bypassed. Both: tuner while enabled (inline),
  spectrum while bypassed (`activeMode()` in ParamDefs.hpp, shared by DSP and GUI). Switching mode or bypassing
  resets the analysis that stops and clears its outputs.
- Audio: soft mute in Tuner mode. While enabled the output fades out (10 ms linear) and stays silent; while bypassed,
  or in Spectrum mode, it fades in and then copies the input unchanged. No latency.
- Scales (`zyngui/zyntuner_scales.py`, also imported by the widget; `make` generates `plugins/zyntuner/Scales.hpp`):
  each is a list of degrees in cents above the root and a period (1200, or the 3:1 tritave). Western tunings put A4
  at the reference; the others put the root at the 12-TET C4 of the reference. 18 scales: 12-TET, just (C),
  Pythagorean (C), 1/4-comma meantone, Werckmeister III, Kirnberger III, Vallotti, 19/24/31/53-TET, 22 shruti,
  slendro, pelog, Thai 7-TET, harmonics 8-15, Partch 43, Bohlen-Pierce. No key/root control (would need a 2nd page).
- Pitch → nearest degree: cents above the root, split into period + remainder, nearest degree (root of the next
  period included).
- Detection on 0.5 * (L + R): 4th order 1.5 kHz low-pass, decimation to ~6 kHz, DC blocker. Signals in opposite
  phase on L and R cancel.
- YIN every 60 ms (65 ms window, 28 to 1350 Hz), lags spread over the blocks. Interpolated dip, then refined on
  multiples of the period. Frames with RMS below -60 dB (fixed since 0.4) skip the analysis.
- Spectrum (`SpectrumAnalyser.hpp`): on 0.5 * (L + R). Real FFT of 4096 points at 44.1 / 48 kHz (8192 at 88.2 kHz
  and above: memory ~130 KB) as an N/2 point complex FFT + split, Hann window, hop N/2 (~43 ms). The work is spread
  over the blocks (window, one FFT stage, bands in two halves per step), finished within half a hop. 32 bands log
  spaced 20 Hz .. 20 kHz: power sum of the bins inside each band (the nearest bin when none), 0 dB = full scale
  sine, clamped to -80 .. 0. Ballistics: instant rise, 40 dB/s fall. Low bands (< ~100 Hz, 1-2 bins) read up to
  ~3 dB low between bins.
- CPU on zynthian-v5 (Pi 5, tests/test_dsp, 48 kHz / 64): tuner 0.19 %, spectrum 0.30 %, bypassed 0.06 %.
- Tests: `tests/run.sh` (built binary, synthetic plucked strings).

## State & presets — AGREED
None.

## Zynthian widget — DONE
Half height: degree name of the selected scale with its octave, cents, needle (clamped at ±50 cents) and the
scale name. Green within 3 cents, orange within 15, red beyond (theme colours).
Spectrum mode (0.4, same half-height widget, items switched by the `mode` control; Both: by
`processor.is_bypassed()`): 32 bars, grid every 20 dB with
labels, frequency labels at 100 / 1k / 10k. Bars green, orange above -12 dB, red above -3 dB. Only changed bars are
redrawn.

## Native GUI (X11, Cairo) — implemented, not tested on a zynthian
Top: the zynthian widget's display (ticks every 10 cents, in-tune zone, needle clamped at ±50 cents, degree name
with octave, cents, scale name; "BYPASSED" while bypassed), or the spectrum bars in Spectrum mode. Bottom strip:
knobs for A4 reference, mode and scale,
a slide switch for bypass, zynthian theme colours. Knobs: vertical drag / wheel (Shift: fine), double-click or
Ctrl+click: default. Degree names come from `Scales.hpp` (generated with the names).

## Help page — implemented, not tested on a zynthian

## Open questions
- Root/key for the just and historical temperaments (fixed to C now)?
- GitHub: https://github.com/riban-bw/zyntuner · release v0.1.0 · 0.2.0 (mute, scales) not released
- Package: zynthian-packages PR: (pending)
