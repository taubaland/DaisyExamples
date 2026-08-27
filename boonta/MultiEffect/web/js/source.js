/** Something to listen to.
 *
 *  The preview needs a signal, and the honest one is whatever you actually
 *  plug in -- so live input is here and is the best of these options. The rest
 *  exist because a browser tab is often nowhere near an interface, and a
 *  reverb tail or the knee of a wavefolder is easier to judge against a note
 *  than against silence.
 *
 *  The plucks are Karplus-Strong: a buffer of noise the length of one period,
 *  averaged with itself as it circulates. It is not a guitar, but it has the
 *  two properties that matter for setting a pedal up -- a transient with real
 *  high-frequency content for the drive to bite on, and a decaying harmonic
 *  tail for the reverb to hang off.
 *
 *  Sweep and noise are here for the EQ, where a note tells you almost nothing:
 *  a filter curve read off a spectrum with signal in one band is a measurement
 *  of the noise floor. `../rig/README.md` has the version of that lesson that
 *  cost a day.
 */

const midiToHz = (note) => 440 * Math.pow(2, (note - 69) / 12);

/** One plucked string, summed into `buf` starting at `start` seconds.
 *
 *  `damping` is the loop's low-pass: lower is a duller, faster-decaying note.
 *  `decay` is the loop gain, which sets sustain. */
const pluck = (buf, sr, start, note, gain, decay = 0.996, damping = 0.5) => {
    const period = Math.max(2, Math.round(sr / midiToHz(note)));
    const line = new Float32Array(period);

    // A pick is broadband but not white -- seeding with white noise gives a
    // spitty attack that no string makes. One pole of smoothing on the seed is
    // enough to sound plucked rather than clicked.
    let z = 0;
    for (let i = 0; i < period; i++) {
        z += 0.6 * ((Math.random() * 2 - 1) - z);
        line[i] = z;
    }

    const from = Math.floor(start * sr);
    const length = Math.min(buf.length - from, Math.floor(sr * 4));
    let p = 0;
    let prev = 0;
    for (let i = 0; i < length; i++) {
        const x = line[p];
        const y = damping * x + (1 - damping) * prev;
        prev = x;
        line[p] = y * decay;
        p = (p + 1) % period;
        buf[from + i] += x * gain;
    }
};

/** A phrase, as [beat, midi note] pairs. */
const RIFF = [
    [0.0, 40], [0.5, 47], [1.0, 50], [1.5, 47],
    [2.0, 40], [2.5, 47], [3.0, 52], [3.25, 50],
    [4.0, 43], [4.5, 50], [5.0, 53], [5.5, 50],
    [6.0, 40], [6.5, 47], [7.0, 45], [7.5, 43],
];

/** Chord voicings, low string first. */
const CHORDS = [
    [0.0, [40, 47, 52, 56, 59, 64]], // E
    [2.0, [43, 50, 55, 59, 62, 67]], // G
    [4.0, [45, 52, 57, 60, 64, 69]], // Am
    [6.0, [38, 45, 50, 57, 62, 66]], // D
];

const BPM = 96;

const makeRiff = (data, sr) => {
    const beat = 60 / BPM;
    for (const [b, note] of RIFF) {
        const gain = 0.32 * (b % 1 === 0 ? 1 : 0.7);
        pluck(data, sr, b * beat, note, gain, 0.9965, 0.52);
    }
};

const makeChords = (data, sr) => {
    const beat = 60 / BPM;
    for (const [b, notes] of CHORDS) {
        notes.forEach((note, i) => {
            // A strum is one pick crossing six strings, so the low string
            // starts a few milliseconds before the high one.
            pluck(data, sr, b * beat + i * 0.012, note, 0.13, 0.997, 0.55);
        });
    }
};

const makeBass = (data, sr) => {
    const beat = 60 / BPM;
    const line = [
        [0.0, 28], [0.75, 28], [1.5, 35], [2.0, 33],
        [3.0, 31], [3.5, 31], [4.0, 26], [5.0, 33],
        [6.0, 28], [6.5, 28], [7.0, 31], [7.5, 33],
    ];
    for (const [b, note] of line) pluck(data, sr, b * beat, note, 0.4, 0.9985, 0.35);
};

/** Logarithmic sweep, 30 Hz to Nyquist-ish. Log rather than linear so every
 *  octave gets the same time, which is what you want when the thing under test
 *  has three sweepable bands. */
const makeSweep = (data, sr) => {
    const seconds = data.length / sr;
    const f0 = 30;
    const f1 = Math.min(18000, sr * 0.45);
    const k = Math.log(f1 / f0);
    let phase = 0;
    for (let i = 0; i < data.length; i++) {
        const t = i / sr / seconds;
        const f = f0 * Math.exp(k * t);
        phase += (2 * Math.PI * f) / sr;
        // Fade the ends, or the discontinuity at the loop point is a click
        // that the drive turns into a bang.
        const fade = Math.min(1, (i / sr) * 20, ((data.length - i) / sr) * 20);
        data[i] = Math.sin(phase) * 0.25 * fade;
    }
};

/** Pink-ish noise: Voss-McCartney with a handful of octaves. Flat per octave,
 *  which is the shape a guitar amp is voiced against. */
const makeNoise = (data, sr) => {
    const rows = 8;
    const values = new Float32Array(rows);
    let running = 0;
    let counter = 0;
    for (let i = 0; i < data.length; i++) {
        counter++;
        for (let r = 0; r < rows; r++) {
            if (counter % (1 << r) === 0) {
                running -= values[r];
                values[r] = Math.random() * 2 - 1;
                running += values[r];
            }
        }
        const fade = Math.min(1, (i / sr) * 20, ((data.length - i) / sr) * 20);
        data[i] = (running / rows) * 0.5 * fade;
    }
};

export const BUILT_IN = [
    { id: 'riff', name: 'Guitar riff', seconds: 6.0, render: makeRiff },
    { id: 'chords', name: 'Chords', seconds: 7.0, render: makeChords },
    { id: 'bass', name: 'Bass line', seconds: 6.0, render: makeBass },
    { id: 'sweep', name: 'Sine sweep', seconds: 6.0, render: makeSweep },
    { id: 'noise', name: 'Pink noise', seconds: 4.0, render: makeNoise },
];

/** Build one of the above as an AudioBuffer.
 *
 *  Mono, duplicated across both channels. The pedal's reverb sums to mono at
 *  its input anyway, and a stereo stimulus would only disguise which side of
 *  the tank a tap is coming from. */
export const renderBuiltIn = (ctx, id) => {
    const spec = BUILT_IN.find((s) => s.id === id) || BUILT_IN[0];
    const length = Math.floor(ctx.sampleRate * spec.seconds);
    const buffer = ctx.createBuffer(2, length, ctx.sampleRate);
    const data = buffer.getChannelData(0);
    spec.render(data, ctx.sampleRate);

    // Keep well clear of full scale: the drive page can add 40 dB, and a
    // stimulus that is already loud turns every gain setting into clipping.
    let peak = 0;
    for (let i = 0; i < length; i++) peak = Math.max(peak, Math.abs(data[i]));
    if (peak > 0) {
        const norm = 0.35 / peak;
        for (let i = 0; i < length; i++) data[i] *= norm;
    }

    buffer.getChannelData(1).set(data);
    return buffer;
};
