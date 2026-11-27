# zyntuner user guide

zyntuner is a tuner for zynthian (and other LV2 hosts). It shows the note you play and how far it is from being in tune. It works with
standard western tuning and with 17 other scales and tunings, from historical temperaments to microtonal and
non-western scales. Like a tuner pedal, it mutes the sound while you tune. It is also a spectrum analyser
(Mode control), showing the level of 32 frequency bands while the sound passes through.

- [Installing](#installing)
- [Adding the tuner to a chain](#adding-the-tuner-to-a-chain)
- [Tuning](#tuning)
- [Spectrum analyser](#spectrum-analyser)
- [Controls](#controls)
- [Scales and tunings](#scales-and-tunings)
- [Troubleshooting](#troubleshooting)
- [Technical details](#technical-details)

## Installing

**From the zynthian package catalogue (recommended).** Install zyntuner from webconf's plugin package list. Updates are
offered there when a new version is released.

**Manually.** Download the bundle for your plugin host from the
[releases page](https://github.com/riban-bw/zyntuner/releases): `zyntuner-lv2-arm64.tar.gz` for Raspberry Pi
based, e.g. zynthian, `zyntuner-lv2-x86_64.tar.gz` for a PC. Install it with the host's installation mechanism, e.g. zynthian webconf's plugin install option.

zyntuner is an **Audio Effect** in the **Analyzer** category.

## Adding the tuner to a chain

Add zyntuner as an audio effect on the chain of the instrument you want to tune, for example the audio input chain
your guitar is plugged into. Put it **first** in the chain, before any distortion, pitch shifter or modulation
effect: these make the pitch harder to read.

A new tuner starts **bypassed**: the sound passes through unchanged until you switch the tuner on. Remember that
**the chain is silent while the tuner is on**. Bypass the tuner again to play. You can switch it on and off:

- with the **Bypass** control on the tuner's page,
- from the chain view, like any other effect,
- with a footswitch or any MIDI controller: MIDI learn the Bypass control, as for any zynthian control.

The sound fades out and in over 10 ms, so switching the tuner never clicks.

If you want to let the sound through while tuning, put zyntuner on a separate chain fed by the same
input.

## Tuning

Select the tuner to show its page. The widget in the lower half of the screen shows:

```
            |    |    |    |    |    ‖    |    |    |    |    |
                                   ███                          
  12-TET                        E 4                  +2.0 ct
```

- **The needle**: how far you are from the nearest note. Left of centre is flat, right is sharp. The scale spans
  -50 to +50 cents, with a tick every 10 cents.
- **The colour**: green within 3 cents (in tune), orange within 15 cents, red beyond.
- **The note**: the nearest note of the selected scale, with its octave (the small number).
- **The cents value** (right): the exact deviation. It can be beyond ±50 cents in scales with wide steps (see below):
  the needle then stays at the edge.
- **The scale** (left): the selected scale.
- **"--"**: no pitch detected (silence, noise, chord, or a signal below -60 dB).

Play **one note at a time** and let it ring. A plucked string reads best a moment after the attack. Tune up to the
note from below: the reading settles as you approach.

## Spectrum analyser

Set **Mode** to **Spectrum** and switch Bypass off. The sound passes through unchanged, and the widget shows the level
of 32 bands from 20 Hz to 20 kHz (about a third of an octave each):

- **Scale**: -80 dB (bottom) to 0 dB (top), a line every 20 dB. 0 dB is a full scale sine wave. Frequency marks at
  100 Hz, 1 kHz and 10 kHz.
- **Bars** rise at once and fall at 40 dB per second. Green, orange above -12 dB, red above -3 dB.
- Below about 100 Hz each bar covers only one or two analysis points: a steady tone there can read up to 3 dB low.

Only the selected mode runs: in Spectrum mode the tuner is stopped, and nothing is analysed while bypassed.

## Controls

zynthian shows the four controls on one page, one per knob:

| Control | Range | Default | What it does |
|---|---|---|---|
| **A4 reference** | 415 .. 466 Hz | 440 Hz | Frequency of A4, the pitch standard. Common values: 440 (modern standard), 442 / 443 (some orchestras), 432, 415 (baroque pitch, a semitone below 440), 466 (a semitone above 440) |
| **Mode** | Tuner / Spectrum | Tuner | Tuner: shows the note, mutes the sound. Spectrum: shows the spectrum, the sound passes |
| **Bypass** | off / on | on | On: the tuner or analyser stops and the sound passes through unchanged. Off: it works (and in Tuner mode the sound is muted) |
| **Scale** | 18 scales | 12-TET | The scale or tuning the notes are compared to (Tuner mode). See below |

The tuner ignores signals below -60 dB (there was a Threshold control for this before version 0.4).

### About the reference

The reference is always the frequency of **A4**, whatever the scale:

- **Western scales** (all the 12-note ones, and 19, 24, 31 and 53-TET): A4 sounds exactly at the reference, and the
  other notes are placed around it by the scale.
- **The other scales** have no A. Their first note (the root) is placed on the equal tempered **C4** of the
  reference: 261.63 Hz with A4 = 440 Hz, 256.87 Hz with A4 = 432 Hz.

So a single control sets the pitch for every scale. To tune a gamelan scale or a raga to a different tonic, move the
reference: one semitone is about 6 % of the frequency (e.g. 440 → 466 Hz raises everything by a semitone).

## Scales and tunings

The tuner finds the note of the selected scale that is nearest to the note you play, and shows how far you are from
it. Each scale is a set of notes repeated every octave (except Bohlen-Pierce, see below).

### Western 12-note tunings

They all use the usual note names (C, C#, D, Eb, E, F, F#, G, G#, A, Bb, B) and keep A4 at the reference.
The just and historical tunings are in the **key of C**: they sound purest in keys close to C.

| Scale | What it is | Use it for |
|---|---|---|
| **12-TET** | Equal temperament: 12 equal semitones of 100 cents | Almost all modern music. The default |
| **Just (C)** | 5-limit just intonation: pure thirds (5/4) and fifths (3/2) from C | Choirs, brass, strings, drones; music staying in or near C |
| **Pythagorean (C)** | Pure fifths (3/2) from Eb to G# | Medieval music; melodic instruments, drones |
| **Meantone 1/4** | Quarter-comma meantone, Eb to G#: pure major thirds | Renaissance and early baroque keyboard music |
| **Werckmeister III** | Well temperament (1691): every key playable, each with its own colour | Baroque keyboard music, Bach |
| **Kirnberger III** | Well temperament (1779), pure thirds and fifths around C | Baroque and classical keyboard music |
| **Vallotti** | Well temperament (18th century), close to equal but with sweeter thirds in common keys | Baroque and classical; a good general historical choice |

In these tunings most notes differ from 12-TET by a few cents, since A stays at the reference and the other notes
move. For example, in just intonation in C with A4 = 440 Hz, C is 16 cents higher than in 12-TET and E only 2 cents
higher: the major third C-E is 14 cents narrower (purer) than in 12-TET. This is intended: tune to the needle, not to
a 12-TET habit.

### Equal temperaments with more notes

| Scale | Step | Note names |
|---|---|---|
| **19-TET** | 63.2 cents | Sharps and flats are different notes: C, C#, Db, D, D#, Eb, E, E#, F... |
| **24-TET** | 50 cents (quarter tones) | 12-TET notes plus a quarter tone sharp: C, C+, C#, C#+, D, D+... Used for Arabic maqam |
| **31-TET** | 38.7 cents | Sharps, flats and "+" / "-" (one step up / down): C, C+, C#, Db, D-, D... A meantone-like tuning |
| **53-TET** | 22.6 cents (about a comma) | Natural notes, sharps and flats, and "+n" / "-n" (n steps of a comma up / down). Close to Pythagorean and just intervals; used for Turkish makam |

With these, the needle can jump between neighbouring notes when you are about half a step away: steps are smaller
than in 12-TET. With 53-TET a step is only 22.6 cents, so the reading changes note at ±11 cents.

### Non-western scales

| Scale | What it is | Note names |
|---|---|---|
| **22 Shruti** | The 22 microtonal steps of Indian classical music (just ratios) | Sa, Re1..Re4, Ga1..Ga4, Ma1..Ma4, Pa, Dha1..Dha4, Ni1..Ni4 (1 = lowest variant, 4 = highest) |
| **Slendro** | Javanese gamelan, 5 notes, here as 5 equal steps of 240 cents | 1, 2, 3, 5, 6 (kepatihan notation) |
| **Pelog** | Javanese gamelan, 7 unequal notes (a typical tuning) | 1 to 7 |
| **Thai 7-TET** | Thai classical music: 7 equal steps of 171 cents | 1 to 7 |

Real gamelans each have their own tuning, so Slendro and Pelog are a typical reference, not the tuning of a
particular ensemble.

### Just and experimental scales

| Scale | What it is | Note names |
|---|---|---|
| **Harmonics 8-15** | Harmonics 8 to 15 of the root: a natural "overtone" scale | The harmonic number: 8 (the root) to 15 |
| **Partch 43** | Harry Partch's 43-tone just scale (11-limit ratios) | The ratio of each note to the root: 1/1, 81/80, 33/32... |
| **Bohlen-Pierce** | 13 equal steps of a "tritave" (3:1, an octave and a fifth) instead of an octave | C, Db, D, E, F, Gb, G, H, Jb, J, A, Bb, B. The small number is the tritave, not the octave |

### Scales with wide steps

In scales with few notes, a note can be far from both of its neighbours: up to 120 cents in Slendro, 140 in Pelog,
100 in Harmonics 8-15. The cents value then goes beyond ±50 and the needle stays at the edge of the scale until you
are within 50 cents of the note. Watch the cents value to bring the note closer.

## Troubleshooting

| Problem | What to try |
|---|---|
| No sound | Normal while the tuner is on: bypass it to play, or use Spectrum mode |
| Always "--" | Check that the chain gets audio (the mixer's level meter) and is above -60 dB. Play a single note, not a chord |
| Notes appear when not playing | Noise or hum above -60 dB: reduce it at the source (gain, cables, noise gate before the tuner) |
| No bars in Spectrum mode | Switch Bypass off. Check that the chain gets audio |
| Note jumps between two names | The note is about half way between two notes of the scale: you are far out of tune, tune towards the needle's side. Scales with many notes (53-TET, Partch 43) do this more often |
| Wrong octave on a bass | Play the open string again and let it ring: the reading settles after the attack. The lowest detected note is about 28 Hz (below a 5-string bass low B) |
| High notes not detected | The highest detected note is about 1350 Hz (E6, the top of a guitar neck) |
| The notes seem off by a few cents in a historical tuning | Intended: these tunings differ from 12-TET. Choose 12-TET to tune to equal temperament |

## Technical details

- **Range**: 28 Hz to 1350 Hz. Accuracy better than 0.5 cent on synthetic plucked strings (tests).
- **Audio**: stereo in and out, no latency. The left and right inputs are mixed for the detection. While enabled the
  output is muted; while bypassed it is an exact copy of the input. Fades: 10 ms.
- **CPU**: very low, on a zynthian V5: about 0.2 % of one core while tuning, 0.3 % in Spectrum mode, 0.06 % bypassed.
- **Spectrum**: 4096 point FFT (8192 at 88.2 kHz and above) with a Hann window, a new analysis every half FFT
  (~43 ms), spread over the audio blocks. Bands: the bins inside each band are summed (power).
- **Plugin URI**: `urn:riban:tuner` (LV2).
- **Output ports** (for other hosts and widgets): `note` (nearest 12-TET MIDI note of the target note), `cents`,
  `valid` (1 = pitch detected), `degree` (index of the note in the scale, 0 = root) and `octave`. Spectrum:
  `band_00` .. `band_31` in dB (-80 .. 0), -80 when not analysing.
- **Native GUI**: the same display as the zynthian widget (tuner or spectrum) above knobs for the reference, mode
  and scale and a bypass switch. Knobs: drag up / down or use the mouse wheel (Shift: fine),
  double-click or Ctrl+click for the default value. Shown on zynthian's VNC display and in desktop hosts.
- The scale tables are in [`zyngui/zyntuner_scales.py`](../zyngui/zyntuner_scales.py), with the pitch of each note
  in cents.
