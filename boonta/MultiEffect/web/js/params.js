/** What the pedal's parameters are, and what a normalised value means.
 *
 *  Every mapping here is a transcription of the firmware: the sweep ranges in
 *  `EqEffect.cpp`, the gain and level curves in `DriveEffect.cpp`, the
 *  exponentials in `ReverbEffect.cpp` and the trims in `Chain.cpp`. The pedal
 *  itself only ever sees a number between 0 and 1 -- these exist so the screen
 *  can say "2.4 kHz" instead of "0.63", and so the audio preview and the
 *  hardware are driven from one description rather than two.
 *
 *  Keep in step with the C++ if a range there ever moves.
 */

// --- shared curves, straight out of the firmware ---------------------------

/** Exponential sweep: an octave is the same distance everywhere on the pot. */
const sweep = (k, lo, hi) => lo * Math.pow(hi / lo, k);

/** Centre detent flat, either end full cut or boost. */
const detentDb = (k, max) => (k - 0.5) * 2 * max;

const linear = (k, lo, hi) => lo + k * (hi - lo);

const db = (gain) => (gain <= 1e-6 ? '-inf' : (20 * Math.log10(gain)).toFixed(1));

const hz = (f) =>
    f >= 1000 ? `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)} kHz` : `${Math.round(f)} Hz`;

const pct = (k) => `${Math.round(k * 100)}%`;

const signedDb = (k) => {
    const v = detentDb(k, 15);
    return `${v >= 0 ? '+' : ''}${v.toFixed(1)} dB`;
};

/** Input trim: unity at centre, +/-12 dB at the ends. Chain.cpp. */
const inputTrim = (k) => Math.pow(4, (k - 0.5) * 2);

/** Output level and drive make-up: silence at the bottom, unity at centre. */
const levelGain = (k) => 4 * k * k;

// --- toggle-dependent tables ----------------------------------------------

/** TOG_SW_1, drive gain range. DriveEffect.cpp. */
const DRIVE_GAIN_MIN = [1, 1, 4];
const DRIVE_GAIN_MAX = [8, 24, 100];

/** TOG_SW_2, reverb tank scale. ReverbEffect.cpp. */
const REVERB_SIZE_SCALE = [0.5, 1.0, 1.6];

/** The three-position switches.
 *
 *  Not in the MIDI map and not in a preset, because they cannot be: they are
 *  physical switches, so their position at power-on is the truth. They are
 *  here because the EQ, drive and reverb all read them, so the preview cannot
 *  sound like the pedal without knowing where they are set.
 */
export const TOGGLES = [
    { id: 'driveRange', name: 'Drive range', hint: 'TOG_SW_1', positions: ['Low', 'Mid', 'High'] },
    { id: 'reverbSize', name: 'Reverb size', hint: 'TOG_SW_2', positions: ['Room', 'Plate', 'Cavern'] },
    { id: 'eqQ', name: 'EQ width', hint: 'TOG_SW_3', positions: ['Wide', 'Medium', 'Tight'] },
];

/** Estimated RT60, from the tank's loop length and the decay coefficient.
 *
 *  One lap of a tank half is del1 + del2 at Dattorro's reference rate, times
 *  the size scale; the tail falls by `decay` per lap. Lands within a few
 *  tenths of the times measured on hardware -- about 2.5 / 5 / 8 seconds at
 *  the top of the knob for room / plate / cavern -- which is close enough for
 *  a number on a screen.
 */
const reverbSeconds = (k, sizeIndex) => {
    const decay = 0.2 + k * (0.7 - 0.2);
    const lap = ((4453 + 3720) / 29761) * REVERB_SIZE_SCALE[sizeIndex];
    return (lap * Math.log(0.001)) / Math.log(decay);
};

// --- the pages -------------------------------------------------------------

export const PAGES = [
    {
        id: 'eq',
        name: 'EQ',
        slot: 0,
        cc: 20,
        blurb: 'Sweepable low shelf, peaking mid and high shelf, each 15 dB either way with the centre detent flat.',
        knobs: [
            { id: 'lowFreq', name: 'Low freq', short: 'LOW F', def: 0.5, format: (k) => hz(sweep(k, 40, 500)) },
            { id: 'midFreq', name: 'Mid freq', short: 'MID F', def: 0.5, format: (k) => hz(sweep(k, 200, 4000)) },
            { id: 'highFreq', name: 'High freq', short: 'HIGH F', def: 0.5, format: (k) => hz(sweep(k, 1500, 12000)) },
            { id: 'lowGain', name: 'Low gain', short: 'LOW G', def: 0.5, centred: true, format: signedDb },
            { id: 'midGain', name: 'Mid gain', short: 'MID G', def: 0.5, centred: true, format: signedDb },
            { id: 'highGain', name: 'High gain', short: 'HIGH G', def: 0.5, centred: true, format: signedDb },
        ],
    },
    {
        id: 'drive',
        name: 'Drive',
        slot: 1,
        cc: 26,
        blurb: 'CHARACTER morphs a soft cubic clip into a hard clip and on into a wavefolder. BIAS is what puts the even harmonics in.',
        knobs: [
            {
                id: 'gain',
                name: 'Gain',
                short: 'GAIN',
                def: 0.35,
                format: (k, t) => {
                    const r = t.driveRange;
                    const g = DRIVE_GAIN_MIN[r] * Math.pow(DRIVE_GAIN_MAX[r] / DRIVE_GAIN_MIN[r], k);
                    return `${g < 10 ? g.toFixed(1) : Math.round(g)}x (${db(g)} dB)`;
                },
            },
            { id: 'tone', name: 'Tone', short: 'TONE', def: 0.5, format: (k) => hz(linear(k, 250, 8000)) },
            {
                id: 'character',
                name: 'Character',
                short: 'CHAR',
                def: 0.25,
                format: (k) =>
                    k < 0.5
                        ? `soft to hard ${Math.round(k * 200)}%`
                        : `hard to fold ${Math.round((k - 0.5) * 200)}%`,
            },
            {
                id: 'bias',
                name: 'Bias',
                short: 'BIAS',
                def: 0.5,
                centred: true,
                format: (k) => {
                    const b = (k - 0.5) * 2 * 0.5;
                    return `${b >= 0 ? '+' : ''}${b.toFixed(2)}`;
                },
            },
            { id: 'level', name: 'Level', short: 'LEVEL', def: 0.5, centred: true, format: (k) => `${db(levelGain(k))} dB` },
            { id: 'mix', name: 'Mix', short: 'MIX', def: 1.0, format: pct },
        ],
    },
    {
        id: 'reverb',
        name: 'Reverb',
        slot: 2,
        cc: 102,
        blurb: 'Dattorro plate. Pre-delay and diffusion fall out of the topology, so they are real controls rather than a mix trick.',
        knobs: [
            { id: 'time', name: 'Time', short: 'TIME', def: 0.5, format: (k, t) => `~${reverbSeconds(k, t.reverbSize).toFixed(1)} s` },
            { id: 'damping', name: 'Damping', short: 'DAMP', def: 0.5, format: (k) => hz(sweep(k, 16000, 500)) },
            { id: 'predelay', name: 'Pre-delay', short: 'PRE', def: 0.0, format: (k) => `${Math.round(k * 250)} ms` },
            { id: 'diffusion', name: 'Diffusion', short: 'DIFF', def: 0.7, format: pct },
            { id: 'lowcut', name: 'Low cut', short: 'LOCUT', def: 0.15, format: (k) => hz(sweep(k, 20, 500)) },
            { id: 'mix', name: 'Mix', short: 'MIX', def: 0.3, format: pct },
        ],
    },
    {
        id: 'meta',
        name: 'Meta',
        slot: null,
        cc: 108,
        blurb: 'The chain as a whole. An amount at zero takes that effect out rather than muting it.',
        knobs: [
            { id: 'eqAmount', name: 'EQ amount', short: 'EQ AMT', def: 1.0, format: pct },
            { id: 'reverbAmount', name: 'Reverb amount', short: 'RVB AMT', def: 1.0, format: pct },
            { id: 'inGain', name: 'In gain', short: 'IN', def: 0.5, centred: true, format: (k) => `${db(inputTrim(k))} dB` },
            { id: 'driveAmount', name: 'Drive amount', short: 'DRV AMT', def: 1.0, format: pct },
            { id: 'outLevel', name: 'Out level', short: 'OUT', def: 0.5, centred: true, format: (k) => `${db(levelGain(k))} dB` },
            { id: 'mix', name: 'Global mix', short: 'MIX', def: 1.0, format: pct },
        ],
    },
];

/** The three effects, in `PedalState::Slot` order. */
export const SLOTS = [
    { id: 'eq', name: 'EQ', page: 0 },
    { id: 'drive', name: 'Drive', page: 1 },
    { id: 'reverb', name: 'Reverb', page: 2 },
];

/** The six permutations, from `PedalState.cpp`. Row = order index, column =
 *  position in the chain. */
export const CHAIN_ORDERS = [
    [0, 1, 2],
    [0, 2, 1],
    [1, 0, 2],
    [1, 2, 0],
    [2, 0, 1],
    [2, 1, 0],
];

export const PRESET_COUNT = 16;

/** How long the pedal waits, with nothing changing, before it commits the live
 *  settings to flash. `kSettleMs` in Storage.cpp. */
export const SETTLE_MS = 2000;

export const defaultParams = () => PAGES.map((p) => p.knobs.map((k) => k.def));
