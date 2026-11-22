#!/usr/bin/env python3
"""Generate a Morningstar MC6 MkII bank file that drives Boonta Beads.

The file format was read off a real bank export (single bank, deviceModel 3).
The `hash` field is Java's String.hashCode of the compact JSON of `data`,
which was checked against that export.

    python make_bank.py            # writes Beads_MC6_MKII_Bank.json
"""
import datetime
import json
import sys

CHANNEL = 1  # Beads listens on every channel

# Message type numbers, as they appear in the editor's export.
TYPE_NONE, TYPE_PC, TYPE_CC = 0, 1, 2
ACTION_NONE, ACTION_PRESS = 0, 1
TOGGLE_ON, TOGGLE_OFF, TOGGLE_ALWAYS = 0, 1, 2


def blank(n):
    return dict(msgNum=n, d1=0, d2=0, d3=0, d4=0, channel=CHANNEL,
                type=TYPE_NONE, action=ACTION_NONE, toggle=TOGGLE_ALWAYS,
                msgInfo="")


def cc(n, number, value, toggle):
    return dict(msgNum=n, d1=number, d2=value, d3=0, d4=0, channel=CHANNEL,
                type=TYPE_CC, action=ACTION_PRESS, toggle=toggle, msgInfo="")


def pc(n, program):
    return dict(msgNum=n, d1=program, d2=0, d3=0, d4=0, channel=CHANNEL,
                type=TYPE_PC, action=ACTION_PRESS, toggle=TOGGLE_ALWAYS,
                msgInfo="")


def pad(msgs):
    msgs = list(msgs)
    return msgs + [blank(i) for i in range(len(msgs), 16)]


def preset(num, name, msgs, toggles):
    return dict(presetNum=num, bankNum=0, isExp=False, shortName=name,
                toggleName=name, longName="", toToggle=toggles,
                toBlink=toggles, toMsgScroll=False, toggleGroup=0,
                msgArray=pad(msgs))


def toggle_cc(num, name, number, on, off):
    return preset(num, name, [cc(0, number, on, TOGGLE_ON),
                              cc(1, number, off, TOGGLE_OFF)], True)


def momentary_cc(num, name, number, value):
    return preset(num, name, [cc(0, number, value, TOGGLE_ALWAYS)], False)


def program(num, name, prog):
    return preset(num, name, [pc(0, prog)], False)


# Beads' controller numbers are in boonta/Beads/MidiMap.h.
PRESETS = [
    toggle_cc(0, "Bypass", 31, 127, 0),   # 127 = in circuit
    toggle_cc(1, "Freeze", 30, 127, 0),
    program(2, "Free", 0),                # pitch quantiser
    program(3, "Chrom", 1),
    program(4, "Harmo", 2),
    toggle_cc(5, "Seed", 26, 127, 0),     # up (+-12 st) / none
    toggle_cc(6, "Fdbk", 27, 127, 0),     # high / off
    toggle_cc(7, "Qual", 28, 127, 0),     # lo-fi / clean
    momentary_cc(8, "Oct-", 22, 32),      # Pitch spans -2..+2 octaves
    momentary_cc(9, "Unis", 22, 64),
    momentary_cc(10, "Oct+", 22, 96),
    toggle_cc(11, "Mix", 25, 127, 64),    # full wet / half
]


def exp_preset(num):
    return dict(presetNum=num, bankNum=0, isExp=True, shortName="EXPRN",
                toggleName="EXPRN", longName="", toToggle=False, toBlink=False,
                toMsgScroll=False, toggleGroup=0,
                msgArray=[dict(blank(i), toggle=TOGGLE_ON) for i in range(16)])


def java_hash(text):
    h = 0
    for unit in text.encode("utf-16-le")[::2]:
        h = (31 * h + unit) & 0xFFFFFFFF
    return h - (1 << 32) if h >= (1 << 31) else h


def build(name="Beads"):
    data = {"bank": {
        "bankNumber": 0, "bankName": name, "bankClearToggle": False,
        "bankMsgArray": [blank(i) for i in range(16)],
        "presetArray": PRESETS,
        "expPresetArray": [exp_preset(0), exp_preset(1)],
    }}
    compact = json.dumps(data, separators=(",", ":"))
    now = datetime.datetime.now(datetime.timezone.utc)
    return {
        "schemaVersion": 1, "dumpType": "singleBank", "deviceModel": 3,
        "downloadDate": now.strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % (now.microsecond // 1000),
        "hash": java_hash(compact), "data": data,
        "description": "Boonta Beads: bypass, freeze, quantiser, seed, feedback, quality, pitch, mix",
    }


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "Beads_MC6_MKII_Bank.json"
    with open(out, "w", encoding="utf-8", newline="") as f:
        f.write(json.dumps(build(), separators=(",", ":")))
    print("wrote", out)
