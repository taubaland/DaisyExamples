/** Widgets. Nothing here knows what a parameter means -- it takes a number
 *  between 0 and 1, draws it, and says when you have changed it. */

const SIZE = 46;
const R = 17;
const CX = SIZE / 2;
const CY = SIZE / 2 + 1;

/** 270 degrees of travel, the gap at the bottom, like a pot with a stop. */
const START = 135;
const SWEEP = 270;

const svgNs = 'http://www.w3.org/2000/svg';

const polar = (deg, r) => {
    const rad = ((deg - 90) * Math.PI) / 180;
    return [CX + r * Math.cos(rad), CY + r * Math.sin(rad)];
};

const arcPath = (fromDeg, toDeg, r) => {
    if (Math.abs(toDeg - fromDeg) < 0.01) return '';
    const [x0, y0] = polar(fromDeg, r);
    const [x1, y1] = polar(toDeg, r);
    const large = Math.abs(toDeg - fromDeg) > 180 ? 1 : 0;
    const sweep = toDeg > fromDeg ? 1 : 0;
    return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
};

export class Knob {
    /** @param spec  a knob from params.js
     *  @param opts  { colour, onChange(value), onCommit() } */
    constructor(spec, opts) {
        this.spec = spec;
        this.opts = opts;
        this.value = spec.def;

        const el = document.createElement('div');
        el.className = 'knob';
        el.tabIndex = 0;
        el.setAttribute('role', 'slider');
        el.setAttribute('aria-label', spec.name);
        el.setAttribute('aria-valuemin', '0');
        el.setAttribute('aria-valuemax', '1');

        const svg = document.createElementNS(svgNs, 'svg');
        svg.setAttribute('width', SIZE);
        svg.setAttribute('height', SIZE + 2);
        svg.setAttribute('viewBox', `0 0 ${SIZE} ${SIZE + 2}`);

        const track = document.createElementNS(svgNs, 'path');
        track.setAttribute('d', arcPath(START, START + SWEEP, R));
        track.setAttribute('fill', 'none');
        track.setAttribute('stroke', '#2b333a');
        track.setAttribute('stroke-width', '4');
        track.setAttribute('stroke-linecap', 'round');

        this.fill = document.createElementNS(svgNs, 'path');
        this.fill.setAttribute('fill', 'none');
        this.fill.setAttribute('stroke', opts.colour);
        this.fill.setAttribute('stroke-width', '4');
        this.fill.setAttribute('stroke-linecap', 'round');

        const body = document.createElementNS(svgNs, 'circle');
        body.setAttribute('cx', CX);
        body.setAttribute('cy', CY);
        body.setAttribute('r', R - 5);
        body.setAttribute('fill', '#12171b');
        body.setAttribute('stroke', '#2b333a');

        this.pointer = document.createElementNS(svgNs, 'line');
        this.pointer.setAttribute('stroke', opts.colour);
        this.pointer.setAttribute('stroke-width', '2');
        this.pointer.setAttribute('stroke-linecap', 'round');

        svg.append(track, this.fill, body, this.pointer);

        this.valueEl = document.createElement('div');
        this.valueEl.className = 'value';

        const label = document.createElement('div');
        label.className = 'label';
        label.textContent = spec.short;

        el.append(svg, label, this.valueEl);
        this.el = el;

        this.bind();
        this.render();
    }

    bind() {
        const el = this.el;
        let dragging = false;
        let lastY = 0;

        el.addEventListener('pointerdown', (e) => {
            dragging = true;
            lastY = e.clientY;
            el.setPointerCapture(e.pointerId);
            el.focus();
            e.preventDefault();
        });

        el.addEventListener('pointermove', (e) => {
            if (!dragging) return;
            const dy = lastY - e.clientY;
            lastY = e.clientY;
            // A full sweep in about 180 px of travel, or 900 with shift held --
            // a 7-bit controller has 128 steps, so fine mode is finer than the
            // wire can carry, which is what you want when auditioning.
            const span = e.shiftKey ? 900 : 180;
            this.set(this.value + dy / span);
        });

        const end = (e) => {
            if (!dragging) return;
            dragging = false;
            el.releasePointerCapture?.(e.pointerId);
            this.opts.onCommit?.();
        };
        el.addEventListener('pointerup', end);
        el.addEventListener('pointercancel', end);

        el.addEventListener('dblclick', () => {
            this.set(this.spec.def);
            this.opts.onCommit?.();
        });

        el.addEventListener('wheel', (e) => {
            e.preventDefault();
            this.set(this.value - Math.sign(e.deltaY) * (e.shiftKey ? 0.002 : 0.02));
            this.opts.onCommit?.();
        }, { passive: false });

        el.addEventListener('keydown', (e) => {
            const step = e.shiftKey ? 0.002 : 0.02;
            let next = null;
            if (e.key === 'ArrowUp' || e.key === 'ArrowRight') next = this.value + step;
            else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') next = this.value - step;
            else if (e.key === 'Home') next = 0;
            else if (e.key === 'End') next = 1;
            else if (e.key === 'Backspace' || e.key === 'Delete') next = this.spec.def;
            if (next === null) return;
            e.preventDefault();
            this.set(next);
            this.opts.onCommit?.();
        });
    }

    /** Set and report. `silent` skips the callback, for when the model changed
     *  underneath us and the widget is catching up. */
    set(v, silent = false) {
        const next = Math.max(0, Math.min(1, v));
        if (next === this.value && silent) return;
        this.value = next;
        this.render();
        if (!silent) this.opts.onChange?.(next);
    }

    /** Redraw against the current toggles, which some readouts depend on --
     *  the drive gain is a different number of dB in each range. */
    render(toggles) {
        if (toggles) this.toggles = toggles;
        const v = this.value;
        const angle = START + v * SWEEP;

        // A centred knob fills from the detent, so "flat" reads as an empty
        // arc rather than as half a boost.
        const from = this.spec.centred ? START + SWEEP / 2 : START;
        this.fill.setAttribute('d', arcPath(Math.min(from, angle), Math.max(from, angle), R));

        const [x0, y0] = polar(angle, 3);
        const [x1, y1] = polar(angle, R - 6);
        this.pointer.setAttribute('x1', x0.toFixed(2));
        this.pointer.setAttribute('y1', y0.toFixed(2));
        this.pointer.setAttribute('x2', x1.toFixed(2));
        this.pointer.setAttribute('y2', y1.toFixed(2));

        const text = this.spec.format(v, this.toggles || { driveRange: 1, reverbSize: 1, eqQ: 1 });
        this.valueEl.textContent = text;
        this.el.setAttribute('aria-valuenow', v.toFixed(3));
        this.el.setAttribute('aria-valuetext', text);
        this.el.title = `${this.spec.name}: ${text}`;
    }
}

/** A row of mutually exclusive buttons. */
export const segmented = (labels, selected, onSelect, titles) => {
    const wrap = document.createElement('div');
    wrap.className = 'seg';
    labels.forEach((label, i) => {
        const b = document.createElement('button');
        b.textContent = label;
        if (titles?.[i]) b.title = titles[i];
        if (i === selected) b.classList.add('sel');
        b.addEventListener('click', () => onSelect(i));
        wrap.append(b);
    });
    return wrap;
};

export const logLine = (host, text) => {
    const line = document.createElement('div');
    const now = new Date();
    const stamp = [now.getHours(), now.getMinutes(), now.getSeconds()]
        .map((n) => String(n).padStart(2, '0'))
        .join(':');
    line.innerHTML = `<span class="t">${stamp}</span>  `;
    line.append(document.createTextNode(text));
    host.append(line);
    host.scrollTop = host.scrollHeight;
    while (host.childElementCount > 200) host.firstElementChild.remove();
};
