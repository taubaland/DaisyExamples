"""Work out what the audio interface can actually do.

Run this first on a new machine, or whenever the rig cannot find a signal. It
lists devices, shows which configurations will open, and scans every input
channel so you can see where things are arriving.

The machine this was written on taught the lesson worth repeating: Windows
exposes a device through four host APIs with different channel counts, and the
one that opens is not necessarily the one wired to the physical jacks. Push 3
offered 16 outputs through MME and DirectSound and 2 through WASAPI, and none of
them reached the output sockets at all.
"""

import sys

import numpy as np
import sounddevice as sd

import rig


def list_devices():
    print("=== all devices ===")
    for i, d in enumerate(sd.query_devices()):
        api = sd.query_hostapis(d["hostapi"])["name"]
        mark = ""
        if i == rig.IN_DEVICE:
            mark += "  <- rig input"
        if i == rig.OUT_DEVICE:
            mark += "  <- rig output"
        print(f"  [{i:3}] {d['name'][:40]:40} {api:20} "
              f"in {d['max_input_channels']:2} out {d['max_output_channels']:2} "
              f"@ {d['default_samplerate']:.0f}{mark}")
    print(f"\nmatching {rig.DEVICE_MATCH!r}, preferring {rig.HOST_API}")
    print(f"  input  -> [{rig.IN_DEVICE}] {sd.query_devices(rig.IN_DEVICE)['name']}")
    print(f"  output -> [{rig.OUT_DEVICE}] {sd.query_devices(rig.OUT_DEVICE)['name']}")


def probe_openable():
    print("\n=== which output configurations open ===")
    for i, d in enumerate(sd.query_devices()):
        if rig.DEVICE_MATCH.lower() not in d["name"].lower():
            continue
        if d["max_output_channels"] < 1:
            continue
        api = sd.query_hostapis(d["hostapi"])["name"]
        for ch in sorted({2, d["max_output_channels"]}):
            for sr in (48000, 44100):
                try:
                    s = sd.OutputStream(samplerate=sr, device=i,
                                        channels=ch, dtype="float32")
                    s.start(); s.stop(); s.close()
                    print(f"  OK   [{i:3}] {api:20} {ch:2} ch @ {sr}")
                except Exception as e:
                    msg = str(e).split("[")[0].strip()[:44]
                    print(f"  fail [{i:3}] {api:20} {ch:2} ch @ {sr}  {msg}")


def scan_inputs(seconds=4.0):
    n_in = sd.query_devices(rig.IN_DEVICE)["max_input_channels"]
    print(f"\n=== input scan: {n_in} channels, {seconds:.0f}s - make noise now ===")
    rec = sd.rec(int(rig.SR * seconds), samplerate=rig.SR, device=rig.IN_DEVICE,
                 channels=n_in, dtype="float32", blocking=True)
    print(f"{'ch':>3}  {'peak dBFS':>10}  {'rms dBFS':>9}   bar")
    for c in range(n_in):
        x = rec[:, c]
        pk, rm = rig.db(rig.peak(x)), rig.db(rig.rms(x))
        bar = "#" * max(0, int((pk + 80) / 4))
        mark = "  <- pedal out" if c == rig.PEDAL_OUT_CH else ""
        print(f"{c+1:3}  {pk:10.1f}  {rm:9.1f}   {bar}{mark}")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "devices"):
        list_devices()
    if what in ("all", "open"):
        probe_openable()
    if what in ("all", "scan"):
        scan_inputs()
