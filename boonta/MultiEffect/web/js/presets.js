/** The librarian.
 *
 *  Sixteen slots, mirroring the sixteen in the pedal's QSPI flash, held in
 *  local storage and readable as JSON.
 *
 *  The pedal cannot be read back, so this is not a view of what is on the
 *  hardware -- it is a bank *you* keep, which can be written onto the
 *  hardware. That distinction matters when the two disagree: turning a knob on
 *  the pedal changes the pedal and not this, and there is no way for the
 *  browser to find out. Write a slot after editing and the two agree again.
 *
 *  ## Writing to the pedal
 *
 *  There is no save gesture in the firmware. The live settings are written
 *  back into whichever preset is current, so writing slot n is:
 *
 *    program change n   -- the pedal saves the slot it is leaving, loads n
 *    every controller   -- which become the live settings, and so become n
 *    wait out the settle window, and the pedal commits n to flash
 *
 *  Recalling n first is not optional. Without it the parameters land in
 *  whatever slot happened to be current, quietly overwriting it.
 */

import { PRESET_COUNT, SETTLE_MS } from './params.js';
import { toPreset, fromPreset, PRESET_FORMAT, PRESET_VERSION } from './state.js';

const BANK_KEY = 'boonta.bank';
export const BANK_FORMAT = 'boonta-multieffect-bank';

/** A few sounds to start from, so the bank is not sixteen blanks.
 *
 *  Each is [page][knob] in the firmware's order, with the chain order and any
 *  slots switched out. Written against the preview rather than the hardware,
 *  so treat them as starting points and not as calibrated tones.
 */
const FACTORY = [
    {
        name: 'Flat + room',
        params: [
            [0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
            [0.35, 0.5, 0.25, 0.5, 0.5, 1.0],
            [0.35, 0.55, 0.08, 0.7, 0.2, 0.22],
            [1.0, 1.0, 0.5, 0.0, 0.5, 1.0],
        ],
        order: 0,
        slotBypassed: [false, true, false],
    },
    {
        name: 'Warm drive',
        params: [
            [0.35, 0.45, 0.5, 0.62, 0.42, 0.55],
            [0.45, 0.42, 0.18, 0.58, 0.5, 1.0],
            [0.3, 0.6, 0.05, 0.7, 0.25, 0.16],
            [1.0, 1.0, 0.5, 1.0, 0.5, 1.0],
        ],
        order: 0,
        slotBypassed: [false, false, false],
    },
    {
        name: 'Fuzz fold',
        params: [
            [0.4, 0.55, 0.45, 0.55, 0.4, 0.5],
            [0.8, 0.35, 0.78, 0.66, 0.42, 1.0],
            [0.45, 0.65, 0.12, 0.75, 0.3, 0.2],
            [1.0, 1.0, 0.5, 1.0, 0.45, 1.0],
        ],
        order: 2,
        slotBypassed: [false, false, false],
    },
    {
        name: 'Cavern wash',
        params: [
            [0.45, 0.5, 0.5, 0.5, 0.45, 0.58],
            [0.2, 0.5, 0.15, 0.5, 0.5, 1.0],
            [0.85, 0.4, 0.45, 0.95, 0.35, 0.62],
            [1.0, 1.0, 0.5, 0.0, 0.5, 1.0],
        ],
        order: 0,
        slotBypassed: [false, true, false],
    },
    {
        name: 'Reverb into drive',
        params: [
            [0.5, 0.5, 0.5, 0.5, 0.5, 0.48],
            [0.5, 0.45, 0.3, 0.5, 0.46, 0.85],
            [0.6, 0.5, 0.2, 0.8, 0.3, 0.5],
            [1.0, 1.0, 0.5, 1.0, 0.5, 1.0],
        ],
        order: 5,
        slotBypassed: [false, false, false],
    },
    {
        name: 'Clean boost',
        params: [
            [0.3, 0.6, 0.55, 0.55, 0.55, 0.58],
            [0.1, 0.7, 0.0, 0.5, 0.62, 0.6],
            [0.3, 0.5, 0.0, 0.7, 0.15, 0.12],
            [1.0, 1.0, 0.62, 1.0, 0.58, 1.0],
        ],
        order: 0,
        slotBypassed: [false, false, false],
    },
];

const factoryBank = () => {
    const presets = new Array(PRESET_COUNT).fill(null);
    FACTORY.forEach((f, i) => {
        presets[i] = {
            format: PRESET_FORMAT,
            version: PRESET_VERSION,
            name: f.name,
            params: f.params.map((row) => row.slice()),
            page: 0,
            order: f.order,
            bypassed: false,
            slotBypassed: f.slotBypassed.slice(),
            toggles: { driveRange: 1, reverbSize: 1, eqQ: 1 },
        };
    });
    return presets;
};

export class Bank {
    constructor(onChange) {
        this.onChange = onChange || (() => {});
        this.presets = this.load();
        /** Which slot the pedal is on, as far as this app knows -- which is to
         *  say, the last one it sent a program change for. Null until then,
         *  because guessing would be worse than admitting we do not know. */
        this.current = null;
    }

    load() {
        try {
            const raw = JSON.parse(localStorage.getItem(BANK_KEY) || 'null');
            if (raw && raw.format === BANK_FORMAT && Array.isArray(raw.presets)) {
                const presets = new Array(PRESET_COUNT).fill(null);
                for (let i = 0; i < PRESET_COUNT; i++) {
                    if (!raw.presets[i]) continue;
                    const { preset } = fromPreset(raw.presets[i]);
                    presets[i] = preset || null;
                }
                return presets;
            }
        } catch {
            // A corrupt bank is not worth a dialog: fall back to the factory
            // set, which is what an empty one would give anyway.
        }
        return factoryBank();
    }

    save() {
        localStorage.setItem(
            BANK_KEY,
            JSON.stringify({ format: BANK_FORMAT, version: 1, presets: this.presets }),
        );
        this.onChange();
    }

    get(index) {
        return this.presets[index] || null;
    }

    /** Snapshot the live model into a slot. */
    store(index, state, name) {
        const existing = this.presets[index];
        this.presets[index] = toPreset(state, name ?? existing?.name ?? `Preset ${index + 1}`);
        this.save();
        return this.presets[index];
    }

    rename(index, name) {
        if (!this.presets[index]) return;
        this.presets[index].name = name;
        this.save();
    }

    clear(index) {
        this.presets[index] = null;
        this.save();
    }

    resetToFactory() {
        this.presets = factoryBank();
        this.save();
    }

    // --- files -------------------------------------------------------------

    exportBank() {
        return JSON.stringify(
            { format: BANK_FORMAT, version: 1, presets: this.presets },
            null,
            2,
        );
    }

    /** Read a whole bank, or a single preset, from a parsed JSON value.
     *
     *  Takes both because both are things you would double-click: a bank is
     *  what you back up, a preset is what you send somebody. `slot` says where
     *  a single preset lands. */
    importJson(raw, slot = 0) {
        if (raw && raw.format === BANK_FORMAT && Array.isArray(raw.presets)) {
            let loaded = 0;
            const next = new Array(PRESET_COUNT).fill(null);
            for (let i = 0; i < PRESET_COUNT; i++) {
                if (!raw.presets[i]) continue;
                const { preset, error } = fromPreset(raw.presets[i]);
                if (error) return { error: `slot ${i + 1}: ${error}` };
                next[i] = preset;
                loaded++;
            }
            this.presets = next;
            this.save();
            return { message: `loaded ${loaded} preset${loaded === 1 ? '' : 's'}` };
        }

        const { preset, error } = fromPreset(raw);
        if (error) return { error };
        this.presets[slot] = preset;
        this.save();
        return { message: `"${preset.name}" into slot ${slot + 1}` };
    }
}

/** Send a slot's worth of settings to the pedal, in the order the firmware
 *  needs, and report when the pedal will have committed it.
 *
 *  Returns the number of milliseconds until the write has settled, so the
 *  caller can say "hold still" rather than leaving you to guess. */
export const writeSlotToPedal = (link, slot, state) => {
    if (!link.connected) return { error: 'no MIDI output selected' };

    link.programChange(slot);
    const sent = link.pushState(state);
    const settleAt = link.scheduledDelayMs + SETTLE_MS;
    return { sent, settleMs: Math.round(settleAt) };
};
