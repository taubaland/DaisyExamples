/** Controller, remote -- the other end of `MidiControl.cpp`.
 *
 *  Turns the model into the messages the pedal listens for. The map is the one
 *  in `MidiMap.h`, transcribed; the two have to agree, and there is no way for
 *  them to agree automatically.
 *
 *  ## What this cannot do
 *
 *  Read the pedal. The firmware has a MIDI *in* and no out -- no SysEx dump,
 *  no CC echo -- so nothing here can ask what the pedal is currently set to.
 *  The browser is therefore the source of truth and the traffic is one way.
 *  That is worth knowing before you trust a screen: it shows what has been
 *  sent, not what has been received.
 *
 *  ## Pacing
 *
 *  A DIN/TRS link runs at 31250 baud, so a three-byte message takes just under
 *  a millisecond of wire time. A full push is thirty of them. Web MIDI takes a
 *  timestamp per message, so they are scheduled a couple of milliseconds apart
 *  rather than handed over in one burst for the interface to buffer or drop.
 *
 *  Knob drags are coalesced instead: at most one message per controller per
 *  frame, keeping the last value. A drag across the whole range is then about
 *  sixty messages rather than a thousand.
 */

import { PAGES, SLOTS, CHAIN_ORDERS, PRESET_COUNT } from './params.js';

// --- the map, from MidiMap.h ----------------------------------------------

export const CC = {
    eq: 20,
    drive: 26,
    reverb: 102,
    meta: 108,
    masterBypass: 114,
    slotBase: 115, // .. 117, in Slot order
    chainOrder: 118,
    pageSelect: 119,
};

/** Which controller carries a given page's knob. */
export const paramCc = (page, knob) => PAGES[page].cc + knob;

/** 0-1 to 0-127. 1.0 must land on 127, or a mix knob at full travel leaves the
 *  pedal a hair short of fully wet. */
export const toSevenBit = (v) => Math.max(0, Math.min(127, Math.round(v * 127)));

/** The inverse of `midimap::Quantise`, aimed at the middle of the band.
 *
 *  The pedal computes `(value * n) / 128`, so any value in a band selects the
 *  same index; picking the centre leaves the most room for a controller or a
 *  merge box that rounds. */
export const quantisedValue = (index, n) =>
    Math.max(0, Math.min(127, Math.round(((index + 0.5) * 128) / n)));

/** A switch CC counts as on from half travel up. Note the sense: the model
 *  stores "bypassed", the wire carries "in circuit". */
export const switchValue = (on) => (on ? 127 : 0);

// --- the port --------------------------------------------------------------

const PORT_KEY = 'boonta.midi.port';
const CHANNEL_KEY = 'boonta.midi.channel';

export class MidiLink {
    constructor(onChange, onLog) {
        this.access = null;
        this.output = null;
        this.channel = Number(localStorage.getItem(CHANNEL_KEY) ?? 0); // 0-15
        this.spacingMs = 2;
        this.onChange = onChange || (() => {});
        this.onLog = onLog || (() => {});
        this.nextSendAt = 0;
        this.pending = new Map(); // cc -> value, coalesced
        this.flushQueued = false;
        this.sentCount = 0;
    }

    get available() {
        return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess;
    }

    get connected() {
        return !!this.output && this.output.state === 'connected';
    }

    async connect() {
        if (!this.available)
            throw new Error(
                'This browser has no Web MIDI. Chrome, Edge and Opera have it; Safari and Firefox do not.',
            );

        // No sysex: nothing here sends any, and asking for it turns a silent
        // permission into a prompt.
        this.access = await navigator.requestMIDIAccess({ sysex: false });
        this.access.onstatechange = () => {
            // A port that vanishes leaves a stale handle that silently swallows
            // everything sent to it, which looks exactly like a firmware bug.
            if (this.output && this.output.state !== 'connected') {
                this.log(`port "${this.output.name}" disconnected`);
                this.output = null;
            }
            this.onChange();
        };

        const remembered = localStorage.getItem(PORT_KEY);
        if (remembered) this.selectPort(remembered, true);
        this.onChange();
        return this.outputs;
    }

    get outputs() {
        return this.access ? [...this.access.outputs.values()] : [];
    }

    selectPort(id, quiet = false) {
        const port = this.outputs.find((p) => p.id === id) || null;
        this.output = port;
        if (port) localStorage.setItem(PORT_KEY, port.id);
        else localStorage.removeItem(PORT_KEY);
        if (!quiet) this.log(port ? `output: ${port.name}` : 'no output selected');
        this.onChange();
        return port;
    }

    setChannel(ch) {
        this.channel = Math.max(0, Math.min(15, ch));
        localStorage.setItem(CHANNEL_KEY, String(this.channel));
    }

    log(text) {
        this.onLog(text);
    }

    // --- sending -----------------------------------------------------------

    /** Schedule one message, spaced from the last. */
    rawSend(bytes, port = this.output) {
        if (!port) return false;
        const now = performance.now();
        const at = Math.max(now, this.nextSendAt);
        this.nextSendAt = at + this.spacingMs;
        try {
            port.send(bytes, at);
            this.sentCount++;
            return true;
        } catch (err) {
            this.log(`send failed: ${err.message}`);
            return false;
        }
    }

    /** Queue a controller change, replacing any value queued for the same
     *  controller this frame.
     *
     *  Flushed on the next frame *or* after a few milliseconds, whichever
     *  comes first. A frame alone is not enough: a browser stops servicing
     *  requestAnimationFrame in a tab that is not visible, so switching away
     *  mid-drag would leave the last controller sitting in the queue and the
     *  pedal a knob-turn behind, indefinitely. */
    sendCc(cc, value) {
        this.pending.set(cc, Math.max(0, Math.min(127, Math.round(value))));
        if (this.flushQueued) return;
        this.flushQueued = true;
        requestAnimationFrame(() => this.flush());
        setTimeout(() => this.flush(), 8);
    }

    /** Send now, skipping the coalescing queue. For one-shot gestures where a
     *  frame of latency would be felt, and for the ordered messages of a full
     *  push. */
    sendCcNow(cc, value) {
        this.pending.delete(cc);
        return this.rawSend([0xb0 | this.channel, cc & 0x7f, Math.max(0, Math.min(127, Math.round(value)))]);
    }

    flush() {
        if (!this.flushQueued) return; // whichever of the two got here second
        this.flushQueued = false;
        for (const [cc, value] of this.pending) this.rawSend([0xb0 | this.channel, cc, value]);
        this.pending.clear();
    }

    programChange(program) {
        if (program < 0 || program >= PRESET_COUNT) return false;
        return this.rawSend([0xc0 | this.channel, program & 0x7f]);
    }

    // --- whole-state pushes -------------------------------------------------

    /** Every controller in the map, in a deliberate order.
     *
     *  Master bypass goes last. A push that arrives while the pedal is in
     *  circuit is audible all the way through -- twenty-four parameters
     *  sweeping past on the way to their destinations -- so if the preset asks
     *  for the pedal to be in circuit, it gets there once everything else has
     *  landed.
     */
    pushState(state) {
        if (!this.output) return 0;
        let n = 0;

        for (let p = 0; p < PAGES.length; p++)
            for (let k = 0; k < PAGES[p].knobs.length; k++)
                if (this.sendCcNow(paramCc(p, k), toSevenBit(state.params[p][k]))) n++;

        for (let s = 0; s < SLOTS.length; s++)
            if (this.sendCcNow(CC.slotBase + s, switchValue(!state.slotBypassed[s]))) n++;

        if (this.sendCcNow(CC.chainOrder, quantisedValue(state.order, CHAIN_ORDERS.length))) n++;
        if (this.sendCcNow(CC.pageSelect, quantisedValue(state.page, PAGES.length))) n++;
        if (this.sendCcNow(CC.masterBypass, switchValue(!state.bypassed))) n++;

        this.log(`pushed ${n} messages`);
        return n;
    }

    /** How long the messages just scheduled will take to go out. */
    get scheduledDelayMs() {
        return Math.max(0, this.nextSendAt - performance.now());
    }
}

/** Which output actually reaches the pedal.
 *
 *  The same job as `rig/midi_probe.py`, and it exists for the same reason: on
 *  a rig where the pedal's TRS input is fed from a controller's MIDI out, only
 *  one of the several ports the computer offers forwards to it, and nothing
 *  about the names says which. Rather than testing one and concluding from
 *  silence, this walks every port sending something you can *see* -- page
 *  select, which moves the page LED and touches no sound.
 *
 *  Calls `onStep` before each port so the caller can name the one to watch.
 */
export const probePorts = async (link, onStep, dwellMs = 1200) => {
    const ports = link.outputs;
    for (const port of ports) {
        onStep(port);
        for (let page = 0; page < PAGES.length; page++) {
            link.rawSend([0xb0 | link.channel, CC.pageSelect, quantisedValue(page, PAGES.length)], port);
            await new Promise((r) => setTimeout(r, dwellMs / PAGES.length));
        }
    }
    onStep(null);
};
