/** Wiring only, the same job as `MultiEffect.cpp`.
 *
 *  The model is in `state.js`, the map in `midi.js`, the bank in `presets.js`,
 *  the DSP in the worklet and the widgets in `ui.js`. This holds one instance
 *  of each and connects them: an edit writes the model, and the model is then
 *  pushed at the MIDI port and at the preview. Both read; neither writes back.
 */

import { PAGES, SLOTS, CHAIN_ORDERS, TOGGLES, PRESET_COUNT, SETTLE_MS } from './params.js';
import { createState, cloneState, applyPreset, sameSound, orderLabel } from './state.js';
import { MidiLink, CC, paramCc, toSevenBit, quantisedValue, switchValue, probePorts } from './midi.js';
import { Bank, writeSlotToPedal } from './presets.js';
import { Preview } from './audio.js';
import { BUILT_IN } from './source.js';
import { Knob, segmented, logLine } from './ui.js';

const $ = (id) => document.getElementById(id);
const PAGE_COLOURS = ['var(--eq)', 'var(--drive)', 'var(--reverb)', 'var(--meta)'];

const state = createState();
const log = (text) => logLine($('log'), text);

const link = new MidiLink(() => refreshMidiUi(), log);
const bank = new Bank(() => renderSlots());
const preview = new Preview((m) => showMeter(m));

let selected = 0;
const knobs = []; // [page][knob]

// --- the editor ------------------------------------------------------------

const buildPages = () => {
    const host = $('pages');
    PAGES.forEach((page, p) => {
        const section = document.createElement('section');
        section.className = 'page';
        section.dataset.page = page.id;

        const head = document.createElement('header');
        const h3 = document.createElement('h3');
        h3.textContent = page.name;
        const cc = document.createElement('span');
        cc.className = 'cc';
        cc.textContent = `CC ${page.cc}-${page.cc + 5}`;
        const blurb = document.createElement('span');
        blurb.className = 'blurb';
        blurb.textContent = page.blurb;
        head.append(h3, cc, blurb);

        const grid = document.createElement('div');
        grid.className = 'knobs';

        knobs[p] = page.knobs.map((spec, k) => {
            const knob = new Knob(spec, {
                colour: PAGE_COLOURS[p],
                onChange: (v) => {
                    state.params[p][k] = v;
                    if (sending()) link.sendCc(paramCc(p, k), toSevenBit(v));
                    onModelChanged({ redrawKnobs: false });
                },
            });
            grid.append(knob.el);
            return knob;
        });

        section.append(head, grid);
        host.append(section);
        page.el = section;
    });
};

/** A page dims when its effect is switched out, so the thing you are editing
 *  and the thing you are hearing do not disagree silently. */
const paintPages = () => {
    PAGES.forEach((page, p) => {
        if (page.slot === null) return;
        page.el.classList.toggle('out', state.slotBypassed[page.slot] || state.bypassed);
    });
};

const refreshKnobs = () => {
    for (let p = 0; p < PAGES.length; p++)
        for (let k = 0; k < PAGES[p].knobs.length; k++) {
            knobs[p][k].set(state.params[p][k], true);
            knobs[p][k].render(state.toggles);
        }
};

/** Redraw only the readouts that depend on a toggle. */
const refreshKnobText = () => {
    for (const row of knobs) for (const knob of row) knob.render(state.toggles);
};

// --- chain, page select, toggles -------------------------------------------

const buildChain = () => {
    const host = $('chain');
    host.replaceChildren();
    CHAIN_ORDERS[state.order].forEach((slot, position) => {
        const el = document.createElement('div');
        el.className = `pos ${SLOTS[slot].id}`;
        if (state.slotBypassed[slot]) el.classList.add('off');
        el.innerHTML = `<span class="n">${position + 1}</span>`;
        el.append(document.createTextNode(SLOTS[slot].name));
        el.title = `${SLOTS[slot].name}: ${state.slotBypassed[slot] ? 'out' : 'in'} (CC ${CC.slotBase + slot})`;
        el.addEventListener('click', () => {
            state.slotBypassed[slot] = !state.slotBypassed[slot];
            if (sending()) link.sendCcNow(CC.slotBase + slot, switchValue(!state.slotBypassed[slot]));
            onModelChanged();
        });
        host.append(el);
    });
    $('orderHint').textContent = `${orderLabel(state.order)} - CC 118`;
};

const buildPageSelect = () => {
    const host = $('pageSelect');
    host.replaceChildren(
        segmented(
            PAGES.map((p) => p.name),
            state.page,
            (i) => {
                state.page = i;
                if (sending()) link.sendCcNow(CC.pageSelect, quantisedValue(i, PAGES.length));
                onModelChanged();
            },
        ),
    );
};

const buildToggles = () => {
    const host = $('toggles');
    host.replaceChildren();
    for (const toggle of TOGGLES) {
        const row = document.createElement('div');
        row.className = 'toggle-row';
        const name = document.createElement('div');
        name.className = 'name';
        name.innerHTML = `${toggle.name}<small>${toggle.hint}</small>`;
        row.append(
            name,
            segmented(toggle.positions, state.toggles[toggle.id], (i) => {
                state.toggles[toggle.id] = i;
                onModelChanged();
            }),
        );
        host.append(row);
    }
};

const paintBypass = () => {
    const b = $('bypass');
    b.classList.toggle('out', state.bypassed);
    b.textContent = state.bypassed ? 'Out of circuit' : 'In circuit';
};

// --- the bank --------------------------------------------------------------

const renderSlots = () => {
    const host = $('slotList');
    host.replaceChildren();
    for (let i = 0; i < PRESET_COUNT; i++) {
        const preset = bank.get(i);
        const li = document.createElement('li');
        li.className = preset ? '' : 'empty';
        if (i === selected) li.classList.add('sel');

        const n = document.createElement('span');
        n.className = 'n';
        n.textContent = i;

        const name = document.createElement('span');
        name.className = 'name';
        name.textContent = preset ? preset.name : 'empty';

        li.append(n, name);

        if (bank.current === i) {
            const here = document.createElement('span');
            here.className = 'here';
            here.textContent = 'PEDAL';
            here.title = 'The last slot this app sent a program change for';
            li.append(here);
        }
        if (i === selected && preset && !sameSound(preset, state)) {
            const edited = document.createElement('span');
            edited.className = 'edited';
            edited.textContent = 'edited';
            edited.title = 'The editor no longer matches this slot';
            li.append(edited);
        }

        li.addEventListener('click', () => {
            selected = i;
            renderSlots();
            refreshButtons();
        });
        li.addEventListener('dblclick', () => loadSlot(i));
        host.append(li);
    }
};

const loadSlot = (i) => {
    const preset = bank.get(i);
    if (!preset) return;
    applyPreset(preset, state);
    selected = i;
    if (sending()) link.pushState(state);
    log(`loaded "${preset.name}" from slot ${i}`);
    onModelChanged();
};

// --- everything downstream of a change -------------------------------------

const onModelChanged = ({ redrawKnobs = true } = {}) => {
    if (redrawKnobs) refreshKnobs();
    buildChain();
    buildPageSelect();
    buildToggles();
    paintBypass();
    paintPages();
    renderSlots();
    refreshKnobText();
    preview.setState(cloneState(state));
};

// --- MIDI ------------------------------------------------------------------

const sending = () => link.connected && $('midiLive').checked;

const refreshMidiUi = () => {
    const outputs = link.outputs;
    const select = $('midiPort');
    select.replaceChildren();

    const none = document.createElement('option');
    none.value = '';
    none.textContent = outputs.length ? 'choose an output' : 'no MIDI outputs';
    select.append(none);

    for (const port of outputs) {
        const option = document.createElement('option');
        option.value = port.id;
        option.textContent = port.name;
        if (link.output && port.id === link.output.id) option.selected = true;
        select.append(option);
    }

    const on = !!link.access;
    select.disabled = !on;
    $('midiChannel').disabled = !on;
    $('midiProbe').disabled = !on || !outputs.length;
    $('midiLive').disabled = !link.connected;
    $('pushAll').disabled = !link.connected;
    $('midiDot').classList.toggle('live', link.connected);
    refreshButtons();
};

const refreshButtons = () => {
    const preset = bank.get(selected);
    $('loadSlot').disabled = !preset;
    $('storeSlot').disabled = false;
    $('renameSlot').disabled = !preset;
    $('clearSlot').disabled = !preset;
    $('exportSlot').disabled = !preset;
    $('recallSlot').disabled = !link.connected;
    $('writeSlot').disabled = !link.connected;
};

const buildChannels = () => {
    const select = $('midiChannel');
    for (let ch = 0; ch < 16; ch++) {
        const option = document.createElement('option');
        option.value = String(ch);
        option.textContent = String(ch + 1);
        if (ch === link.channel) option.selected = true;
        select.append(option);
    }
    select.addEventListener('change', () => {
        link.setChannel(Number(select.value));
        log(`channel ${link.channel + 1} (the pedal is omni unless midimap::kChannel says otherwise)`);
    });
};

// --- audio -----------------------------------------------------------------

const showMeter = ({ in: peakIn, out: peakOut }) => {
    const paint = (el, peak) => {
        // A decibel scale over the top 60 dB, so a quiet signal still moves the
        // bar. Linear would show a guitar as a sliver and clipping as full.
        const dbfs = peak > 0 ? 20 * Math.log10(peak) : -120;
        const width = Math.max(0, Math.min(1, (dbfs + 60) / 60));
        el.style.width = `${width * 100}%`;
        el.classList.toggle('hot', peak >= 0.99);
    };
    paint($('meterIn'), peakIn);
    paint($('meterOut'), peakOut);
};

const buildSources = () => {
    const select = $('sourceSelect');
    for (const source of BUILT_IN) {
        const option = document.createElement('option');
        option.value = source.id;
        option.textContent = source.name;
        select.append(option);
    }
    select.addEventListener('change', async () => {
        await preview.useBuiltIn(select.value);
        note($('audioNote'), `${select.options[select.selectedIndex].text}, generated at ${preview.sampleRate} Hz`);
    });
};

const note = (el, text, warn = false) => {
    el.textContent = text;
    el.classList.toggle('warn', warn);
};

const setPlaying = (on) => {
    $('playBtn').textContent = on ? 'Stop' : 'Play';
    $('playBtn').classList.toggle('primary', !on);
};

// --- events ----------------------------------------------------------------

const wire = () => {
    $('midiEnable').addEventListener('click', async () => {
        try {
            await link.connect();
            $('midiEnable').textContent = 'MIDI on';
            $('midiEnable').disabled = true;
            const outputs = link.outputs;
            log(`${outputs.length} MIDI output${outputs.length === 1 ? '' : 's'}: ${outputs.map((p) => p.name).join(', ') || 'none'}`);
            if (!outputs.length)
                log('nothing to send to. The pedal listens on its TRS/DIN input, so you need an interface with a MIDI out.');
        } catch (err) {
            log(`MIDI unavailable: ${err.message}`);
        }
    });

    $('midiPort').addEventListener('change', (e) => link.selectPort(e.target.value));

    $('midiProbe').addEventListener('click', async () => {
        $('midiProbe').disabled = true;
        $('midiDot').classList.add('busy');
        log('probing: watch the pedal, the page LED steps EQ / Drive / Reverb / Meta on the port that reaches it');
        await probePorts(link, (port) => {
            if (port) log(`  trying "${port.name}"`);
        });
        $('midiDot').classList.remove('busy');
        $('midiProbe').disabled = false;
        log('probe done. Select the port whose turn the pedal reacted on.');
    });

    $('pushAll').addEventListener('click', () => {
        link.pushState(state);
        note($('writeNote'), 'Pushed the editor to the pedal. This did not change which preset slot is current.');
    });

    $('bypass').addEventListener('click', () => {
        state.bypassed = !state.bypassed;
        if (sending()) link.sendCcNow(CC.masterBypass, switchValue(!state.bypassed));
        onModelChanged();
    });

    const stepOrder = (delta) => {
        state.order = (state.order + delta + CHAIN_ORDERS.length) % CHAIN_ORDERS.length;
        if (sending()) link.sendCcNow(CC.chainOrder, quantisedValue(state.order, CHAIN_ORDERS.length));
        onModelChanged();
    };
    $('orderPrev').addEventListener('click', () => stepOrder(-1));
    $('orderNext').addEventListener('click', () => stepOrder(1));

    // --- bank ---------------------------------------------------------------

    $('loadSlot').addEventListener('click', () => loadSlot(selected));

    $('storeSlot').addEventListener('click', () => {
        const existing = bank.get(selected);
        const name = existing?.name ?? prompt('Name this preset', `Preset ${selected + 1}`);
        if (name === null) return;
        bank.store(selected, state, name);
        log(`stored the editor into slot ${selected} ("${bank.get(selected).name}")`);
        note($('writeNote'), 'Stored in the browser. "Write to pedal" is what puts it on the hardware.');
        renderSlots();
        refreshButtons();
    });

    $('recallSlot').addEventListener('click', () => {
        link.programChange(selected);
        bank.current = selected;
        log(`program change ${selected}`);
        note(
            $('writeNote'),
            'Sent a program change. The pedal has loaded its own slot ' +
                selected +
                ", which is not necessarily what the browser holds -- the editor was left alone so the two can be compared.",
        );
        renderSlots();
    });

    $('writeSlot').addEventListener('click', () => {
        const { error, sent, settleMs } = writeSlotToPedal(link, selected, state);
        if (error) return log(`write failed: ${error}`);
        bank.current = selected;
        // Store the browser's copy too, or the bank would show a slot that
        // disagrees with the hardware it was just written to.
        if (!bank.get(selected)) bank.store(selected, state, `Preset ${selected + 1}`);
        else bank.store(selected, state, bank.get(selected).name);
        log(`wrote slot ${selected}: program change then ${sent} controllers`);
        note(
            $('writeNote'),
            `Leave the pedal alone for about ${(settleMs / 1000).toFixed(1)} s. It commits to flash ${SETTLE_MS / 1000} s after the last change, and a knob touched in the meantime is what gets saved instead.`,
        );
        renderSlots();
    });

    $('renameSlot').addEventListener('click', () => {
        const preset = bank.get(selected);
        const name = prompt('Rename', preset.name);
        if (name === null) return;
        bank.rename(selected, name);
        renderSlots();
    });

    $('clearSlot').addEventListener('click', () => {
        if (!confirm(`Clear slot ${selected}? This affects the browser only.`)) return;
        bank.clear(selected);
        renderSlots();
        refreshButtons();
    });

    // --- files --------------------------------------------------------------

    const download = (filename, text) => {
        const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.click();
        URL.revokeObjectURL(url);
    };

    const safe = (name) => name.replace(/[^\w.-]+/g, '-').toLowerCase() || 'preset';

    $('exportSlot').addEventListener('click', () => {
        const preset = bank.get(selected);
        download(`boonta-${safe(preset.name)}.json`, JSON.stringify(preset, null, 2));
    });

    $('exportBank').addEventListener('click', () => download('boonta-bank.json', bank.exportBank()));

    $('importFile').addEventListener('click', () => $('importInput').click());

    $('importInput').addEventListener('change', async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        e.target.value = '';
        try {
            const raw = JSON.parse(await file.text());
            const { error, message } = bank.importJson(raw, selected);
            if (error) return log(`import failed: ${error}`);
            log(`imported ${message}`);
            renderSlots();
            refreshButtons();
        } catch (err) {
            log(`import failed: ${err.message}`);
        }
    });

    $('factory').addEventListener('click', () => {
        if (!confirm('Replace the whole bank with the starting set?')) return;
        bank.resetToFactory();
        renderSlots();
        refreshButtons();
        log('bank reset to the starting set');
    });

    // --- preview ------------------------------------------------------------

    $('playBtn').addEventListener('click', async () => {
        if (preview.playing) {
            preview.stop();
            setPlaying(false);
            note($('audioNote'), 'Stopped.');
            return;
        }
        try {
            await preview.init();
            preview.setState(cloneState(state));
            if (!preview.buffer && !preview.stream) await preview.useBuiltIn($('sourceSelect').value);
            await preview.play();
            setPlaying(true);
            note($('audioNote'), `Running at ${preview.sampleRate} Hz. The pedal runs at 48000.`);
        } catch (err) {
            note($('audioNote'), `Could not start: ${err.message}`, true);
        }
    });

    $('loopBox').addEventListener('change', (e) => preview.setLoop(e.target.checked));
    $('volume').addEventListener('input', (e) => preview.setVolume(Number(e.target.value)));

    $('loadAudio').addEventListener('click', () => $('audioInput').click());
    $('audioInput').addEventListener('change', async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        e.target.value = '';
        try {
            const buffer = await preview.useFile(file);
            note($('audioNote'), `${file.name} - ${buffer.duration.toFixed(1)} s, ${buffer.numberOfChannels} ch`);
        } catch (err) {
            note($('audioNote'), `Could not decode ${file.name}: ${err.message}`, true);
        }
    });

    $('liveInput').addEventListener('click', async () => {
        try {
            await preview.useLiveInput();
            note(
                $('audioNote'),
                'Live input. Use headphones -- the preview goes to your speakers, and a microphone plus speakers is a feedback loop.',
                true,
            );
            if (!preview.playing) {
                await preview.play();
                setPlaying(true);
            }
        } catch (err) {
            note($('audioNote'), `No input: ${err.message}`, true);
        }
    });

    preview.onEnded = () => setPlaying(false);
};

// --- go --------------------------------------------------------------------

buildPages();
buildChannels();
buildSources();
wire();
onModelChanged();
refreshMidiUi();

if (!link.available)
    log('This browser has no Web MIDI. Chrome, Edge and Opera have it; Safari and Firefox do not. Everything except sending still works.');
else log('Ready. Enable MIDI to choose an output, or just use the preview.');
