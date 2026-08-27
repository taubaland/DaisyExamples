/** The pedal's DSP, in the browser.
 *
 *  A port of `Chain.cpp`, `EqEffect.cpp`, `DriveEffect.cpp`, `ReverbEffect.cpp`
 *  and `Biquad.h` -- same topology, same coefficients, same order of
 *  operations. It exists so you can hear what a setting does before sending it
 *  to a pedal you may not be standing next to, and so a preset can be dialled
 *  in with headphones on.
 *
 *  Two things stop it being the pedal:
 *
 *  - The firmware runs at 48 kHz and this runs at whatever the browser's audio
 *    context is. Everything is a function of `sampleRate`, as it is on the
 *    hardware, so the sound tracks -- but a tank whose lengths are rounded to
 *    a different grid is not sample-identical.
 *  - There is no codec, no relay and no analog path. The pedal's input stage
 *    and its output stage are not modelled, because nothing in this repository
 *    describes them.
 *
 *  So: close enough to choose a sound with, not close enough to measure with.
 *  `../rig` is what measures.
 *
 *  Block size is 128 frames, which is both the firmware's `kMaxBlockSize` and
 *  the Web Audio render quantum, so the control-rate updates land in the same
 *  places they do on the pedal.
 */

const TWO_PI = 6.28318530718;

// --- Biquad.h --------------------------------------------------------------

/** RBJ cookbook sections, transposed direct form II. Coefficients and state
 *  are separate for the same reason as in the firmware: both channels are
 *  filtered identically, so a stereo band is one design and two histories. */
class BiquadCoeffs {
    constructor() {
        this.setIdentity();
    }

    setIdentity() {
        this.b0 = 1;
        this.b1 = this.b2 = this.a1 = this.a2 = 0;
    }

    /** Held short of Nyquist so a knob at full travel cannot design an
     *  unstable section. */
    static omega(sr, freq) {
        const limit = sr * 0.49;
        const f = Math.min(Math.max(freq, 1), limit);
        return (TWO_PI * f) / sr;
    }

    normalise(nb0, nb1, nb2, na0, na1, na2) {
        const inv = 1 / na0;
        this.b0 = nb0 * inv;
        this.b1 = nb1 * inv;
        this.b2 = nb2 * inv;
        this.a1 = na1 * inv;
        this.a2 = na2 * inv;
    }

    setPeaking(sr, freq, gainDb, q) {
        const a = Math.pow(10, gainDb / 40);
        const w0 = BiquadCoeffs.omega(sr, freq);
        const cs = Math.cos(w0);
        const alpha = Math.sin(w0) / (2 * q);
        this.normalise(
            1 + alpha * a, -2 * cs, 1 - alpha * a,
            1 + alpha / a, -2 * cs, 1 - alpha / a,
        );
    }

    setLowShelf(sr, freq, gainDb, slope) {
        const a = Math.pow(10, gainDb / 40);
        const w0 = BiquadCoeffs.omega(sr, freq);
        const cs = Math.cos(w0);
        const alpha = Math.sin(w0) * 0.5 * Math.sqrt((a + 1 / a) * (1 / slope - 1) + 2);
        const beta = 2 * Math.sqrt(a) * alpha;
        this.normalise(
            a * (a + 1 - (a - 1) * cs + beta),
            2 * a * (a - 1 - (a + 1) * cs),
            a * (a + 1 - (a - 1) * cs - beta),
            a + 1 + (a - 1) * cs + beta,
            -2 * (a - 1 + (a + 1) * cs),
            a + 1 + (a - 1) * cs - beta,
        );
    }

    setHighShelf(sr, freq, gainDb, slope) {
        const a = Math.pow(10, gainDb / 40);
        const w0 = BiquadCoeffs.omega(sr, freq);
        const cs = Math.cos(w0);
        const alpha = Math.sin(w0) * 0.5 * Math.sqrt((a + 1 / a) * (1 / slope - 1) + 2);
        const beta = 2 * Math.sqrt(a) * alpha;
        this.normalise(
            a * (a + 1 + (a - 1) * cs + beta),
            -2 * a * (a - 1 + (a + 1) * cs),
            a * (a + 1 + (a - 1) * cs - beta),
            a + 1 - (a - 1) * cs + beta,
            2 * (a - 1 - (a + 1) * cs),
            a + 1 - (a - 1) * cs - beta,
        );
    }
}

class BiquadState {
    constructor() {
        this.z1 = 0;
        this.z2 = 0;
    }

    reset() {
        this.z1 = this.z2 = 0;
    }

    process(c, x) {
        const y = c.b0 * x + this.z1;
        this.z1 = c.b1 * x - c.a1 * y + this.z2;
        this.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
}

// --- DaisySP DelayLine ------------------------------------------------------

/** The write pointer walks backwards and `read` interpolates, matching
 *  `DaisySP::DelayLine` -- the allpasses read before they write, so getting
 *  this the other way round shifts every tank length by a sample. */
class DelayLine {
    constructor(size) {
        this.size = size;
        this.buf = new Float32Array(size);
        this.w = 0;
    }

    reset() {
        this.buf.fill(0);
        this.w = 0;
    }

    write(x) {
        this.buf[this.w] = x;
        this.w = (this.w - 1 + this.size) % this.size;
    }

    read(delay) {
        const i = delay | 0;
        const f = delay - i;
        const a = this.buf[(this.w + i) % this.size];
        const b = this.buf[(this.w + i + 1) % this.size];
        return a + (b - a) * f;
    }
}

// --- EqEffect.cpp -----------------------------------------------------------

const EQ_LOW_MIN = 40, EQ_LOW_MAX = 500;
const EQ_MID_MIN = 200, EQ_MID_MAX = 4000;
const EQ_HIGH_MIN = 1500, EQ_HIGH_MAX = 12000;
const EQ_MAX_GAIN_DB = 15;
const EQ_SHELF_SLOPE = [0.4, 0.7, 1.0];
const EQ_MID_Q = [0.5, 1.1, 3.0];
const REDESIGN_EPSILON = 0.001;

const sweepHz = (knob, lo, hi) => lo * Math.pow(hi / lo, knob);
const gainDb = (knob) => (knob - 0.5) * 2 * EQ_MAX_GAIN_DB;

class EqEffect {
    constructor(sampleRate) {
        this.sr = sampleRate;
        this.low = new BiquadCoeffs();
        this.mid = new BiquadCoeffs();
        this.high = new BiquadCoeffs();
        this.lowZ = [new BiquadState(), new BiquadState()];
        this.midZ = [new BiquadState(), new BiquadState()];
        this.highZ = [new BiquadState(), new BiquadState()];
        this.from = [0, 0, 0, 0, 0, 0];
        this.fromQ = -1;
        this.valid = false;
    }

    updateCoeffs(p, qIndex) {
        let changed = !this.valid || qIndex !== this.fromQ;
        for (let i = 0; i < 6 && !changed; i++)
            changed = Math.abs(p[i] - this.from[i]) > REDESIGN_EPSILON;
        if (!changed) return;

        for (let i = 0; i < 6; i++) this.from[i] = p[i];
        this.fromQ = qIndex;
        this.valid = true;

        const slope = EQ_SHELF_SLOPE[qIndex];
        this.low.setLowShelf(this.sr, sweepHz(p[0], EQ_LOW_MIN, EQ_LOW_MAX), gainDb(p[3]), slope);
        this.mid.setPeaking(this.sr, sweepHz(p[1], EQ_MID_MIN, EQ_MID_MAX), gainDb(p[4]), EQ_MID_Q[qIndex]);
        this.high.setHighShelf(this.sr, sweepHz(p[2], EQ_HIGH_MIN, EQ_HIGH_MAX), gainDb(p[5]), slope);
    }

    process(p, qIndex, left, right, size) {
        this.updateCoeffs(p, qIndex);
        const channels = [left, right];
        for (let c = 0; c < 2; c++) {
            const buf = channels[c];
            const lz = this.lowZ[c], mz = this.midZ[c], hz = this.highZ[c];
            for (let i = 0; i < size; i++) {
                let x = buf[i];
                x = lz.process(this.low, x);
                x = mz.process(this.mid, x);
                x = hz.process(this.high, x);
                buf[i] = x;
            }
        }
    }
}

// --- DriveEffect.cpp --------------------------------------------------------

const DRIVE_GAIN_MIN = [1, 1, 4];
const DRIVE_GAIN_MAX = [8, 24, 100];
const TONE_MIN_HZ = 250, TONE_MAX_HZ = 8000;
const MAX_BIAS = 0.5;
const DC_BLOCK_HZ = 10;

/** Soft cubic clip, normalised so it reaches unity at the knee. */
const softShape = (x) => (x >= 1 ? 1 : x <= -1 ? -1 : 1.5 * (x - (x * x * x) / 3));
const hardShape = (x) => (x > 1 ? 1 : x < -1 ? -1 : x);

/** Triangle wavefolder, closed form -- at the top of the gain range the input
 *  can be a hundred times full scale, and a reflect-until-inside loop that long
 *  has no place in an audio callback. */
const foldShape = (x) => {
    const u = (x + 1) * 0.25;
    return 4 * Math.abs(u - Math.floor(u + 0.5)) - 1;
};

const levelGain = (knob) => 4 * knob * knob;

class DriveEffect {
    constructor(sampleRate) {
        this.sr = sampleRate;
        this.dcCoeff = (TWO_PI * DC_BLOCK_HZ) / sampleRate;
        this.toneZ = [0, 0];
        this.dcZ = [0, 0];
        this.gain = 1;
        this.bias = 0;
        this.character = 0;
        this.toneCoeff = 1;
        this.level = 1;
        this.mix = 1;
    }

    process(p, range, left, right, size) {
        this.gain =
            DRIVE_GAIN_MIN[range] *
            Math.pow(DRIVE_GAIN_MAX[range] / DRIVE_GAIN_MIN[range], p[0]);
        this.bias = (p[3] - 0.5) * 2 * MAX_BIAS;
        this.character = p[2];
        const toneHz = TONE_MIN_HZ + p[1] * (TONE_MAX_HZ - TONE_MIN_HZ);
        this.toneCoeff = Math.min(1, (TWO_PI * toneHz) / this.sr);
        this.level = levelGain(p[4]);
        this.mix = p[5];

        for (let i = 0; i < size; i++) {
            left[i] = this.processSample(0, left[i]);
            right[i] = this.processSample(1, right[i]);
        }
    }

    processSample(channel, input) {
        const driven = input * this.gain + this.bias;

        // Morph across the three shapers: soft to hard over the lower half of
        // the knob, hard to fold over the upper half.
        let wet;
        if (this.character < 0.5) {
            const t = this.character * 2;
            const s = softShape(driven);
            wet = s + t * (hardShape(driven) - s);
        } else {
            const t = (this.character - 0.5) * 2;
            const h = hardShape(driven);
            wet = h + t * (foldShape(driven) - h);
        }

        // Remove the offset the bias put in, plus whatever asymmetric clipping
        // added on its own.
        this.dcZ[channel] += this.dcCoeff * (wet - this.dcZ[channel]);
        wet -= this.dcZ[channel];

        this.toneZ[channel] += this.toneCoeff * (wet - this.toneZ[channel]);
        wet = this.toneZ[channel] * this.level;

        return input + this.mix * (wet - input);
    }
}

// --- ReverbEffect.cpp -------------------------------------------------------

const REF_RATE = 29761;
const DIF1 = 142, DIF2 = 107, DIF3 = 379, DIF4 = 277;
const AP1L = 672, DEL1L = 4453, AP2L = 1800, DEL2L = 3720;
const AP1R = 908, DEL1R = 4217, AP2R = 2656, DEL2R = 3163;
const TAP_L = [266, 2974, 1913, 1996, 1066, 913, 378];
const TAP_R = [353, 3627, 1228, 2673, 2111, 335, 121];
const SIZE_SCALE = [0.5, 1.0, 1.6];
const SIZE_TRIM = [0.8, 1.6, 0.6];
const DECAY_MAX = 0.7, DECAY_MIN = 0.2;
const PREDELAY_MAX_MS = 250;
const DAMP_MIN_HZ = 16000, DAMP_MAX_HZ = 500;
const LOWCUT_MIN_HZ = 20, LOWCUT_MAX_HZ = 500;
const BANDWIDTH_HZ = 9000;
const REVERB_INPUT_TRIM = 0.5;
const MOD_DEPTH = 8;
const MOD_RATE_L = 0.71, MOD_RATE_R = 1.13;

const poleCoeff = (sr, hz) => Math.min(1, (TWO_PI * hz) / sr);
const exponential = (knob, lo, hi) => lo * Math.pow(hi / lo, knob);

/** Schroeder allpass over a delay line, fractional so the modulated sections
 *  can move without stepping. */
const apProcess = (line, delay, coeff, x) => {
    const read = line.read(delay);
    const write = x + coeff * read;
    line.write(write);
    return read - coeff * write;
};

class ReverbEffect {
    constructor(sampleRate) {
        this.sr = sampleRate;

        this.predelay = new DelayLine(32768);
        this.dif1 = new DelayLine(512);
        this.dif2 = new DelayLine(512);
        this.dif3 = new DelayLine(2048);
        this.dif4 = new DelayLine(1024);
        this.ap1l = new DelayLine(4096);
        this.del1l = new DelayLine(16384);
        this.ap2l = new DelayLine(8192);
        this.del2l = new DelayLine(16384);
        this.ap1r = new DelayLine(4096);
        this.del1r = new DelayLine(16384);
        this.ap2r = new DelayLine(16384);
        this.del2r = new DelayLine(16384);

        this.lowcutZ = 0;
        this.bandZ = 0;
        this.dampLZ = 0;
        this.dampRZ = 0;
        this.lfoL = 0;
        this.lfoR = 0.25;
        this.bandwidthCoeff = poleCoeff(sampleRate, BANDWIDTH_HZ);
        this.unit = sampleRate / REF_RATE;

        this.predelaySamples = 1;
        this.decay = 0.5;
        this.dampCoeff = 1;
        this.lowcutCoeff = 0.01;
        this.inDiff1 = 0.75;
        this.inDiff2 = 0.625;
        this.decDiff1 = 0.7;
        this.decDiff2 = 0.5;
        this.mix = 0.3;
        this.sizeTrim = 1;
        this.modL = 0;
        this.modR = 0;

        this.tapL = new Float32Array(7);
        this.tapR = new Float32Array(7);
    }

    updateCoeffs(p, sizeIndex, size) {
        this.unit = (this.sr / REF_RATE) * SIZE_SCALE[sizeIndex];
        this.sizeTrim = SIZE_TRIM[sizeIndex];

        this.decay = DECAY_MIN + p[0] * (DECAY_MAX - DECAY_MIN);
        this.dampCoeff = poleCoeff(this.sr, exponential(p[1], DAMP_MIN_HZ, DAMP_MAX_HZ));
        this.lowcutCoeff = poleCoeff(this.sr, exponential(p[4], LOWCUT_MIN_HZ, LOWCUT_MAX_HZ));

        let pre = p[2] * PREDELAY_MAX_MS * this.sr * 0.001;
        if (pre > 32760) pre = 32760;
        if (pre < 1) pre = 1;
        this.predelaySamples = pre;

        // Diffusion moves the input diffusers and the tank allpasses together:
        // fully down is a handful of discrete echoes, fully up is Dattorro's
        // own coefficients and a smooth wash.
        const d = p[3];
        this.inDiff1 = 0.25 + 0.5 * d;
        this.inDiff2 = 0.2 + 0.425 * d;
        this.decDiff1 = 0.2 + 0.5 * d;
        this.decDiff2 = 0.15 + 0.35 * d;

        this.mix = p[5];

        const blockSeconds = size / this.sr;
        this.lfoL += MOD_RATE_L * blockSeconds;
        this.lfoR += MOD_RATE_R * blockSeconds;
        this.lfoL -= Math.floor(this.lfoL);
        this.lfoR -= Math.floor(this.lfoR);
        this.modL = MOD_DEPTH * Math.sin(TWO_PI * this.lfoL);
        this.modR = MOD_DEPTH * Math.sin(TWO_PI * this.lfoR);
    }

    process(p, sizeIndex, left, right, size) {
        this.updateCoeffs(p, sizeIndex, size);

        const unit = this.unit;
        const len = (reference, max) => {
            const l = reference * unit;
            return l > max - 2 ? max - 2 : l < 1 ? 1 : l;
        };

        const lDif1 = len(DIF1, 512), lDif2 = len(DIF2, 512);
        const lDif3 = len(DIF3, 2048), lDif4 = len(DIF4, 1024);
        const lAp1l = len(AP1L, 4096), lDel1l = len(DEL1L, 16384);
        const lAp2l = len(AP2L, 8192), lDel2l = len(DEL2L, 16384);
        const lAp1r = len(AP1R, 4096), lDel1r = len(DEL1R, 16384);
        const lAp2r = len(AP2R, 16384), lDel2r = len(DEL2R, 16384);

        for (let i = 0; i < 7; i++) {
            this.tapL[i] = len(TAP_L[i], 8192);
            this.tapR[i] = len(TAP_R[i], 8192);
        }
        const tl = this.tapL, tr = this.tapR;
        const tapGain = 0.6 * this.sizeTrim;

        for (let i = 0; i < size; i++) {
            const dryL = left[i];
            const dryR = right[i];

            // A plate is one plate. Sum to mono in, take stereo out of the taps.
            let x = (dryL + dryR) * 0.5 * REVERB_INPUT_TRIM;

            this.predelay.write(x);
            x = this.predelay.read(this.predelaySamples);

            // Low cut, as a one-pole highpass: track the lows, subtract them.
            this.lowcutZ += this.lowcutCoeff * (x - this.lowcutZ);
            x -= this.lowcutZ;

            this.bandZ += this.bandwidthCoeff * (x - this.bandZ);
            x = this.bandZ;

            x = apProcess(this.dif1, lDif1, this.inDiff1, x);
            x = apProcess(this.dif2, lDif2, this.inDiff1, x);
            x = apProcess(this.dif3, lDif3, this.inDiff2, x);
            x = apProcess(this.dif4, lDif4, this.inDiff2, x);

            // Read both cross-feeds before writing anything, so each half sees
            // the other as it was one lap ago rather than half-updated.
            const fbL = this.del2l.read(lDel2l);
            const fbR = this.del2r.read(lDel2r);

            const a = apProcess(this.ap1l, lAp1l + this.modL, -this.decDiff1, x + fbR);
            this.del1l.write(a);
            let b = this.del1l.read(lDel1l);
            this.dampLZ += this.dampCoeff * (b - this.dampLZ);
            b = this.dampLZ * this.decay;
            const c = apProcess(this.ap2l, lAp2l, this.decDiff2, b);
            this.del2l.write(c);

            const dd = apProcess(this.ap1r, lAp1r + this.modR, -this.decDiff1, x + fbL);
            this.del1r.write(dd);
            let e = this.del1r.read(lDel1r);
            this.dampRZ += this.dampCoeff * (e - this.dampRZ);
            e = this.dampRZ * this.decay;
            const f = apProcess(this.ap2r, lAp2r, this.decDiff2, e);
            this.del2r.write(f);

            const wetL =
                tapGain *
                (this.del1r.read(tl[0]) + this.del1r.read(tl[1]) - this.ap2r.read(tl[2]) +
                    this.del2r.read(tl[3]) - this.del1l.read(tl[4]) - this.ap2l.read(tl[5]) -
                    this.del2l.read(tl[6]));

            const wetR =
                tapGain *
                (this.del1l.read(tr[0]) + this.del1l.read(tr[1]) - this.ap2l.read(tr[2]) +
                    this.del2l.read(tr[3]) - this.del1r.read(tr[4]) - this.ap2r.read(tr[5]) -
                    this.del2r.read(tr[6]));

            left[i] = dryL + this.mix * (wetL - dryL);
            right[i] = dryR + this.mix * (wetR - dryR);
        }
    }
}

// --- Chain.cpp --------------------------------------------------------------

const CHAIN_ORDERS = [
    [0, 1, 2],
    [0, 2, 1],
    [1, 0, 2],
    [1, 2, 0],
    [2, 0, 1],
    [2, 1, 0],
];

/** Which meta knob carries which slot's amount. */
const SLOT_AMOUNT_KNOB = [0, 3, 1];

/** Unity at the centre of the pot, 12 dB either way at the ends. A trim, not a
 *  level, so it never mutes. */
const inputTrim = (knob) => Math.pow(4, (knob - 0.5) * 2);

const BLOCK = 128;

class BoontaProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        this.eq = new EqEffect(sampleRate);
        this.drive = new DriveEffect(sampleRate);
        this.reverb = new ReverbEffect(sampleRate);

        this.dryL = new Float32Array(BLOCK);
        this.dryR = new Float32Array(BLOCK);
        this.slotL = new Float32Array(BLOCK);
        this.slotR = new Float32Array(BLOCK);
        this.workL = new Float32Array(BLOCK);
        this.workR = new Float32Array(BLOCK);

        this.state = {
            params: [
                [0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
                [0.35, 0.5, 0.25, 0.5, 0.5, 1.0],
                [0.5, 0.5, 0.0, 0.7, 0.15, 0.3],
                [1.0, 1.0, 0.5, 1.0, 0.5, 1.0],
            ],
            order: 0,
            bypassed: false,
            slotBypassed: [false, false, false],
            toggles: { driveRange: 1, reverbSize: 1, eqQ: 1 },
        };

        this.peakIn = 0;
        this.peakOut = 0;
        this.blocksSinceReport = 0;

        this.port.onmessage = (event) => {
            const msg = event.data;
            if (msg.type === 'state') this.state = msg.state;
        };
    }

    /** What a slot contributes once bypass has had its say. */
    slotAmount(slot) {
        return this.state.slotBypassed[slot] ? 0 : this.state.params[3][SLOT_AMOUNT_KNOB[slot]];
    }

    runSlot(slot, size) {
        const s = this.state;
        if (slot === 0) this.eq.process(s.params[0], s.toggles.eqQ, this.workL, this.workR, size);
        else if (slot === 1)
            this.drive.process(s.params[1], s.toggles.driveRange, this.workL, this.workR, size);
        else this.reverb.process(s.params[2], s.toggles.reverbSize, this.workL, this.workR, size);
    }

    process(inputs, outputs) {
        const output = outputs[0];
        const outL = output[0];
        const outR = output[1] || output[0];
        const size = outL.length;

        const input = inputs[0];
        const inL = input && input[0] ? input[0] : null;
        const inR = input && input[1] ? input[1] : inL;

        if (!inL) {
            outL.fill(0);
            if (outR !== outL) outR.fill(0);
            return true;
        }

        const s = this.state;

        // Hard bypass: on the pedal the relays route around the DSP, so this is
        // a pass-through and the filter state is left to settle.
        if (s.bypassed) {
            outL.set(inL.subarray(0, size));
            if (outR !== outL) outR.set(inR.subarray(0, size));
            this.report(inL, outL, size);
            return true;
        }

        const trim = inputTrim(s.params[3][2]);
        for (let i = 0; i < size; i++) {
            this.dryL[i] = inL[i];
            this.dryR[i] = inR[i];
            this.workL[i] = inL[i] * trim;
            this.workR[i] = inR[i] * trim;
        }

        for (let position = 0; position < 3; position++) {
            const slot = CHAIN_ORDERS[s.order][position];
            const amount = this.slotAmount(slot);

            for (let i = 0; i < size; i++) {
                this.slotL[i] = this.workL[i];
                this.slotR[i] = this.workR[i];
            }

            // Every slot runs even when bypassed or at zero amount. Skipping
            // would save cycles and freeze the reverb tank mid-tail, so
            // switching the reverb back in would resume a stale tail rather
            // than a decayed one.
            this.runSlot(slot, size);

            for (let i = 0; i < size; i++) {
                this.workL[i] = this.slotL[i] + amount * (this.workL[i] - this.slotL[i]);
                this.workR[i] = this.slotR[i] + amount * (this.workR[i] - this.slotR[i]);
            }
        }

        const level = 4 * s.params[3][4] * s.params[3][4];
        const mix = s.params[3][5];

        for (let i = 0; i < size; i++) {
            const wetL = this.workL[i] * level;
            const wetR = this.workR[i] * level;
            outL[i] = this.dryL[i] + mix * (wetL - this.dryL[i]);
            if (outR !== outL) outR[i] = this.dryR[i] + mix * (wetR - this.dryR[i]);
        }

        this.report(inL, outL, size);
        return true;
    }

    /** Peak in and out, a few times a second.
     *
     *  Peak rather than RMS because the question a meter answers here is "is
     *  the drive clipping the output", and an average will not say. */
    report(inL, outL, size) {
        for (let i = 0; i < size; i++) {
            const a = Math.abs(inL[i]);
            const b = Math.abs(outL[i]);
            if (a > this.peakIn) this.peakIn = a;
            if (b > this.peakOut) this.peakOut = b;
        }
        if (++this.blocksSinceReport >= 16) {
            this.port.postMessage({ type: 'meter', in: this.peakIn, out: this.peakOut });
            this.blocksSinceReport = 0;
            this.peakIn = 0;
            this.peakOut = 0;
        }
    }
}

registerProcessor('boonta-processor', BoontaProcessor);
