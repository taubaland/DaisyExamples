"""Measurement rig for the Boonta pedal, via the Push 3 as an audio interface.

Signal path under test:
    PC -> Push out 1/2 -> pedal in -> pedal out -> Push in 1/2 -> PC

Everything here is measurement only; it never assumes the pedal is doing
anything in particular, so it can tell "the pedal is wrong" apart from "there is
no signal path at all", which is the distinction that matters first.
"""

import os

import numpy as np
import sounddevice as sd

SR = 48000

#: Substring matched against device names to find the interface. Override with
#: the BOONTA_DEVICE environment variable for a different one.
DEVICE_MATCH = os.environ.get("BOONTA_DEVICE", "Push")

#: Preferred host API. WASAPI is the one that behaved on the machine this was
#: written on; MME exposes more channels but is higher latency.
HOST_API = os.environ.get("BOONTA_HOSTAPI", "WASAPI")


def _find(match, host_api, want_input):
    """Locate a device by name rather than by index, because indices move when
    anything else is plugged in or a driver reloads."""
    best = None
    for i, d in enumerate(sd.query_devices()):
        if match.lower() not in d["name"].lower():
            continue
        chans = d["max_input_channels"] if want_input else d["max_output_channels"]
        if chans < 1:
            continue
        api = sd.query_hostapis(d["hostapi"])["name"]
        score = (host_api.lower() in api.lower(), chans)
        if best is None or score > best[0]:
            best = (score, i)
    if best is None:
        raise RuntimeError(
            f"no {'input' if want_input else 'output'} device matching "
            f"{match!r}. Run interface.py to list what is available.")
    return best[1]


IN_DEVICE = int(os.environ["BOONTA_IN_DEVICE"]) if "BOONTA_IN_DEVICE" in os.environ \
    else _find(DEVICE_MATCH, HOST_API, want_input=True)
OUT_DEVICE = int(os.environ["BOONTA_OUT_DEVICE"]) if "BOONTA_OUT_DEVICE" in os.environ \
    else _find(DEVICE_MATCH, HOST_API, want_input=False)

#: Which input channel the pedal's output is patched to, zero-based.
PEDAL_OUT_CH = int(os.environ.get("BOONTA_PEDAL_OUT_CH", "1"))


def _in_channels():
    return sd.query_devices(IN_DEVICE)["max_input_channels"]


def play_record(signal, in_channels=None, extra_tail=0.25):
    """Play a mono signal on out 1+2, record the return. Returns (L, R).

    A tail of silence is appended so anything with a decay - the reverb, most
    obviously - is not truncated at the moment the stimulus stops.
    """
    if in_channels is None:
        in_channels = _in_channels()

    tail = np.zeros(int(SR * extra_tail), dtype=np.float32)
    mono = np.concatenate([signal.astype(np.float32), tail])
    stereo_out = np.column_stack([mono, mono])

    rec = sd.playrec(
        stereo_out,
        samplerate=SR,
        device=(IN_DEVICE, OUT_DEVICE),
        channels=in_channels,
        dtype="float32",
        blocking=True,
    )
    return rec[:, 0], rec[:, 1]


def record_only(seconds, in_channels=None):
    if in_channels is None:
        in_channels = _in_channels()
    rec = sd.rec(
        int(SR * seconds),
        samplerate=SR,
        device=IN_DEVICE,
        channels=in_channels,
        dtype="float32",
        blocking=True,
    )
    return rec[:, 0], rec[:, 1]


def tone(freq, seconds, amp=0.25, fade_ms=10):
    n = int(SR * seconds)
    t = np.arange(n) / SR
    x = amp * np.sin(2 * np.pi * freq * t)
    # Fade the ends so the stimulus has no click of its own to confuse things.
    f = int(SR * fade_ms / 1000)
    if f > 0 and 2 * f < n:
        ramp = np.linspace(0, 1, f)
        x[:f] *= ramp
        x[-f:] *= ramp[::-1]
    return x.astype(np.float32)


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def peak(x):
    return float(np.max(np.abs(x))) if len(x) else 0.0


def db(x, ref=1.0):
    x = max(float(x), 1e-12)
    return 20.0 * np.log10(x / ref)


def goertzel(x, freq):
    """Magnitude at one frequency. Cheaper and less leaky than an FFT bin when
    the stimulus frequency is known exactly."""
    n = len(x)
    k = 2 * np.pi * freq / SR
    t = np.arange(n)
    i = np.sum(x * np.cos(k * t))
    q = np.sum(x * np.sin(k * t))
    return 2.0 * np.hypot(i, q) / n


def thd(x, fundamental, harmonics=6):
    """Total harmonic distortion as a fraction, from the harmonic magnitudes."""
    f0 = goertzel(x, fundamental)
    if f0 <= 1e-9:
        return float("nan")
    hs = []
    for h in range(2, harmonics + 1):
        f = fundamental * h
        if f < SR / 2:
            hs.append(goertzel(x, f))
    return float(np.sqrt(np.sum(np.square(hs))) / f0)


def steady(x, skip=0.25, take=0.5):
    """Middle slice of a recording, past any onset transient and settling."""
    a = int(len(x) * skip)
    b = a + int(len(x) * take)
    return x[a:b]
