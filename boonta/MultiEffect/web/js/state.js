/** Model.
 *
 *  The same shape as `PedalState` on the pedal, and for the same reason: one
 *  description of what the thing is set to, which the MIDI sender, the preset
 *  bank and the audio preview all read. Nothing here knows about the DOM, Web
 *  MIDI or Web Audio.
 *
 *  Two departures from the firmware's model, both because the browser is not
 *  the pedal:
 *
 *  - The toggles live here. On the pedal they are physical switches and cannot
 *    be set remotely or saved; here they are a statement of where yours are,
 *    so the preview knows which drive range and reverb size to run.
 *  - `page` is carried, because page select is in the MIDI map and moving it
 *    is a real thing to do to a pedal with one screen's worth of LEDs. It has
 *    no effect on the sound.
 */

import { PAGES, SLOTS, CHAIN_ORDERS, defaultParams } from './params.js';

export const createState = () => ({
    /** [page][knob], every one between 0 and 1. */
    params: defaultParams(),
    page: 0,
    order: 0,
    /** True means out of circuit. Matches the firmware's sense, which is the
     *  opposite of the CC's -- CC 114 at 127 means *in*. */
    bypassed: false,
    slotBypassed: [false, false, false],
    toggles: { driveRange: 1, reverbSize: 1, eqQ: 1 },
});

export const cloneState = (s) => ({
    params: s.params.map((page) => page.slice()),
    page: s.page,
    order: s.order,
    bypassed: s.bypassed,
    slotBypassed: s.slotBypassed.slice(),
    toggles: { ...s.toggles },
});

export const orderLabel = (order) =>
    CHAIN_ORDERS[order].map((slot) => SLOTS[slot].name).join(' -> ');

// --- presets ---------------------------------------------------------------

/** What a preset is on disk.
 *
 *  Deliberately the same fields as `SavedState` in the firmware, plus a name
 *  the pedal has no way to store -- sixteen unlabelled slots on a box with no
 *  screen is exactly the problem a librarian exists to solve, and the name
 *  never has to leave the browser.
 *
 *  The toggles are recorded too, but as a note rather than a setting: they
 *  cannot be recalled, so on load they are offered rather than applied.
 */
export const PRESET_FORMAT = 'boonta-multieffect-preset';
export const PRESET_VERSION = 1;

export const toPreset = (s, name) => ({
    format: PRESET_FORMAT,
    version: PRESET_VERSION,
    name: name || 'Untitled',
    params: s.params.map((page) => page.slice()),
    page: s.page,
    order: s.order,
    bypassed: s.bypassed,
    slotBypassed: s.slotBypassed.slice(),
    toggles: { ...s.toggles },
});

const clamp01 = (v) => (typeof v === 'number' && v >= 0 && v <= 1 ? v : null);

/** Read a preset back, rejecting anything malformed rather than letting a
 *  half-parsed file put NaN into a parameter and out of the MIDI port.
 *
 *  Returns { preset } or { error }. */
export const fromPreset = (raw) => {
    if (!raw || typeof raw !== 'object') return { error: 'not an object' };
    if (raw.format !== PRESET_FORMAT) return { error: 'not a Boonta preset' };
    if (raw.version !== PRESET_VERSION) return { error: `unknown version ${raw.version}` };
    if (!Array.isArray(raw.params) || raw.params.length !== PAGES.length)
        return { error: 'wrong number of pages' };

    const params = [];
    for (let p = 0; p < PAGES.length; p++) {
        const row = raw.params[p];
        if (!Array.isArray(row) || row.length !== PAGES[p].knobs.length)
            return { error: `page ${PAGES[p].name} has the wrong number of knobs` };
        const clamped = row.map(clamp01);
        if (clamped.some((v) => v === null))
            return { error: `page ${PAGES[p].name} has a value outside 0..1` };
        params.push(clamped);
    }

    const inRange = (v, n) => Number.isInteger(v) && v >= 0 && v < n;
    if (!inRange(raw.page, PAGES.length)) return { error: 'bad page' };
    if (!inRange(raw.order, CHAIN_ORDERS.length)) return { error: 'bad chain order' };
    if (!Array.isArray(raw.slotBypassed) || raw.slotBypassed.length !== SLOTS.length)
        return { error: 'bad slot bypass list' };

    return {
        preset: {
            format: PRESET_FORMAT,
            version: PRESET_VERSION,
            name: typeof raw.name === 'string' ? raw.name : 'Untitled',
            params,
            page: raw.page,
            order: raw.order,
            bypassed: !!raw.bypassed,
            slotBypassed: raw.slotBypassed.map(Boolean),
            toggles: {
                driveRange: inRange(raw.toggles?.driveRange, 3) ? raw.toggles.driveRange : 1,
                reverbSize: inRange(raw.toggles?.reverbSize, 3) ? raw.toggles.reverbSize : 1,
                eqQ: inRange(raw.toggles?.eqQ, 3) ? raw.toggles.eqQ : 1,
            },
        },
    };
};

/** Push a preset into the live model.
 *
 *  `withToggles` is off by default. Recalling a preset on the pedal cannot move
 *  a physical switch, so applying the stored positions here would make the
 *  preview disagree with the hardware -- quietly, and in the one place you are
 *  listening to decide whether the sound is right. */
export const applyPreset = (preset, state, withToggles = false) => {
    state.params = preset.params.map((page) => page.slice());
    state.page = preset.page;
    state.order = preset.order;
    state.bypassed = preset.bypassed;
    state.slotBypassed = preset.slotBypassed.slice();
    if (withToggles) state.toggles = { ...preset.toggles };
    return state;
};

/** Do the two describe the same sound? Used to mark a slot as edited.
 *
 *  Compared with a tolerance for the same reason `SavedState::operator!=` uses
 *  one: values arrive from a 7-bit CC and go back out through a slider, so an
 *  exact comparison calls a round trip a change. */
const EPSILON = 0.002;

export const sameSound = (a, b) => {
    if (!a || !b) return false;
    if (a.order !== b.order || !!a.bypassed !== !!b.bypassed) return false;
    for (let i = 0; i < a.slotBypassed.length; i++)
        if (!!a.slotBypassed[i] !== !!b.slotBypassed[i]) return false;
    for (let p = 0; p < a.params.length; p++)
        for (let k = 0; k < a.params[p].length; k++)
            if (Math.abs(a.params[p][k] - b.params[p][k]) > EPSILON) return false;
    return true;
};
