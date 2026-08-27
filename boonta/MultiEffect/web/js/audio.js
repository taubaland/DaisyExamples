/** The preview: a signal, the pedal's DSP, and your speakers.
 *
 *  Wiring only. The DSP is in `../worklet/boonta-processor.js`, the stimulus is
 *  in `source.js`, and neither knows about the other.
 *
 *      source -> [worklet] -> output gain -> destination
 *
 *  The model is pushed into the worklet whole on every change rather than
 *  parameter by parameter. It is a few hundred bytes, changes at most once per
 *  animation frame, and one message means the worklet can never be running
 *  half of one preset and half of another -- which, on a chain whose order can
 *  change, would be audible.
 */

import { renderBuiltIn } from './source.js';

const WORKLET_URL = new URL('../worklet/boonta-processor.js', import.meta.url);

export class Preview {
    constructor(onMeter) {
        this.ctx = null;
        this.node = null;
        this.gain = null;
        this.source = null;
        this.buffer = null;
        this.stream = null;
        this.playing = false;
        this.loop = true;
        this.onMeter = onMeter || (() => {});
        this.pendingState = null;
        /** The build in flight, so concurrent init()s share one. */
        this.building = null;
        /** Held here rather than only on the GainNode, so a rebuilt graph comes
         *  back at the volume the slider is actually showing. */
        this.volume = 0.8;
    }

    get ready() {
        return !!this.node;
    }

    get sampleRate() {
        return this.ctx ? this.ctx.sampleRate : 0;
    }

    /** Build the graph. Must be called from a user gesture, or the context
     *  starts suspended and nothing is heard.
     *
     *  Guards on `node`, not on `ctx`, and that distinction is the whole point.
     *  The context is created synchronously but the worklet module loads over
     *  the network, so there is a window in which `ctx` exists and the graph
     *  does not. Testing `ctx` there returns "already built" for an object with
     *  no processor in it, and the next `play()` calls `connect(undefined)` --
     *  which surfaces as "Failed to execute 'connect' on 'AudioNode': Overload
     *  resolution failed", naming neither the real fault nor the file it is in.
     *
     *  Concurrent calls share one build rather than racing two contexts, and a
     *  build that fails leaves nothing behind, so pressing Play again is a
     *  genuine retry instead of a permanent version of the same error. */
    async init() {
        if (this.node) {
            if (this.ctx.state === 'suspended') await this.ctx.resume();
            return;
        }

        if (!this.building)
            this.building = this.build().finally(() => {
                this.building = null;
            });
        await this.building;

        if (this.ctx.state === 'suspended') await this.ctx.resume();
    }

    async build() {
        const ctx = new AudioContext({ latencyHint: 'interactive' });

        try {
            try {
                await ctx.audioWorklet.addModule(WORKLET_URL);
            } catch (err) {
                // Overwhelmingly the common cause, and the one the browser's
                // own message is worst at: the page is not being served, or is
                // being served from somewhere the worklet is not under.
                throw new Error(
                    `could not load the DSP worklet from ${WORKLET_URL}. `
                        + 'Is the page being served? Opening index.html as a file will not work '
                        + `(${err.message})`,
                );
            }

            const node = new AudioWorkletNode(ctx, 'boonta-processor', {
                numberOfInputs: 1,
                numberOfOutputs: 1,
                outputChannelCount: [2],
            });
            node.port.onmessage = (event) => {
                if (event.data.type === 'meter') this.onMeter(event.data);
            };

            const gain = ctx.createGain();
            gain.gain.value = this.volume;

            node.connect(gain).connect(ctx.destination);

            // Publish all three together. Nothing outside should ever see a
            // context without a graph hanging off it.
            this.ctx = ctx;
            this.node = node;
            this.gain = gain;

            if (this.pendingState) this.setState(this.pendingState);
        } catch (err) {
            // A browser allows only a handful of live contexts, so a retry loop
            // that leaked one per attempt would eventually fail for a second,
            // unrelated reason.
            try {
                await ctx.close();
            } catch {
                // Already closed, or never opened far enough to close.
            }
            this.ctx = null;
            this.node = null;
            this.gain = null;
            throw err;
        }
    }

    setState(state) {
        this.pendingState = state;
        if (!this.node) return;
        this.node.port.postMessage({ type: 'state', state });
    }

    setVolume(v) {
        this.volume = v;
        if (this.gain) this.gain.gain.value = v;
    }

    // --- sources ------------------------------------------------------------

    async useBuiltIn(id) {
        await this.init();
        this.releaseStream();
        this.buffer = renderBuiltIn(this.ctx, id);
        this.restartIfPlaying();
    }

    /** Decode a dropped or chosen file. Whatever the browser can decode -- wav,
     *  mp3, flac, m4a -- which is a longer list than anything shipped here. */
    async useFile(file) {
        await this.init();
        const bytes = await file.arrayBuffer();
        const buffer = await this.ctx.decodeAudioData(bytes);
        this.releaseStream();
        this.buffer = buffer;
        this.restartIfPlaying();
        return buffer;
    }

    /** An interface input, which is the only stimulus that is really yours.
     *
     *  Everything the browser would normally do to a microphone is turned off.
     *  Echo cancellation and noise suppression are voice processing: on a
     *  guitar they gate the tail of every note and duck the signal whenever
     *  the speakers make a sound, and automatic gain control undoes exactly
     *  the input level you came here to set. */
    async useLiveInput() {
        await this.init();
        const stream = await navigator.mediaDevices.getUserMedia({
            audio: {
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl: false,
                channelCount: 2,
            },
        });
        this.releaseStream();
        this.stream = stream;
        this.buffer = null;
        this.restartIfPlaying();
        return stream;
    }

    releaseStream() {
        if (this.stream) {
            this.stream.getTracks().forEach((t) => t.stop());
            this.stream = null;
        }
    }

    // --- transport ----------------------------------------------------------

    async play() {
        await this.init();

        // init() either builds the graph or throws, so this cannot fire -- but
        // it is the assertion that turns a future regression into a sentence
        // instead of "Overload resolution failed" from three frames deeper.
        if (!this.node) throw new Error('the audio graph is not built');

        this.stopSource();

        if (this.stream) {
            this.source = this.ctx.createMediaStreamSource(this.stream);
        } else if (this.buffer) {
            const node = this.ctx.createBufferSource();
            node.buffer = this.buffer;
            node.loop = this.loop;
            node.onended = () => {
                if (this.source === node && !this.loop) {
                    this.playing = false;
                    this.source = null;
                    this.onEnded?.();
                }
            };
            this.source = node;
        } else {
            return false;
        }

        this.source.connect(this.node);
        if (this.source.start) this.source.start();
        this.playing = true;
        return true;
    }

    stop() {
        this.stopSource();
        this.playing = false;
    }

    stopSource() {
        if (!this.source) return;
        try {
            if (this.source.stop) this.source.stop();
        } catch {
            // Already stopped; a buffer source that has ended throws on stop.
        }
        this.source.disconnect();
        this.source = null;
    }

    restartIfPlaying() {
        if (this.playing) this.play();
    }

    setLoop(on) {
        this.loop = on;
        if (this.source && 'loop' in this.source) this.source.loop = on;
    }
}
