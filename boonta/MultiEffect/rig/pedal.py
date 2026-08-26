"""Drive the pedal's model over the ST-Link, so audio tests can set it up
without anyone touching a knob.

Soft pickup is the wrinkle: an *armed* knob is rewritten from its pot on every
audio block, so writing a parameter the pot currently owns is undone within a
millisecond. Everything here parks all six knobs first - a parked knob holds its
stored value until the pot sweeps through it, and the pots are not moving - and
only then writes values.
"""

import os
import subprocess

#: The firmware's ELF, for symbol names. Resolved relative to this file so the
#: rig works from a clone anywhere; override with BOONTA_ELF.
ELF = os.environ.get(
    "BOONTA_ELF",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 os.pardir, "build", "MultiEffect.elf"))

GDB = os.environ.get("BOONTA_GDB", "arm-none-eabi-gdb")

PAGE_EQ, PAGE_DRIVE, PAGE_REVERB, PAGE_META = 0, 1, 2, 3
SLOT_EQ, SLOT_DRIVE, SLOT_REVERB = 0, 1, 2


def _run(cmds, timeout=90):
    args = [GDB, ELF, "-batch", "-ex", "target extended-remote :3333", "-ex", "monitor halt"]
    for c in cmds:
        args += ["-ex", c]
    args += ["-ex", "monitor resume", "-ex", "detach"]
    p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    return p.stdout + p.stderr


def read_pots():
    """Current smoothed pot positions, as the firmware sees them."""
    out = _run(['printf "POTS %.4f %.4f %.4f %.4f %.4f %.4f\\n", '
                'hw.knob[0].val_, hw.knob[1].val_, hw.knob[2].val_, '
                'hw.knob[3].val_, hw.knob[4].val_, hw.knob[5].val_'])
    for line in out.splitlines():
        p = line.split()
        if p and p[0] == "POTS":
            return [float(x) for x in p[1:]]
    raise RuntimeError("could not read pots:\n" + out)


def _park_cmds(pots, page, values):
    """Park every knob of `page` against the values we are about to write.

    Clearing armed_ alone is not enough. Pickup also re-arms on *crossing*, and
    that test compares the current pot-versus-stored sign against
    entered_above_, which still holds whatever was true when the page was last
    entered. Leave it stale and the knob re-arms on the very next block and
    snaps back to the pot, silently undoing the write. So set entered_above_ to
    match the value being written, and since the pots are not moving, the sign
    never flips and the knob stays parked.
    """
    cmds = []
    for i in range(6):
        stored = values[i] if values and values[i] is not None else None
        if stored is None:
            continue
        cmds.append(f"set var state.param_[{page}][{i}] = {stored}")
        cmds.append(f"set var controls.armed_[{i}] = 0")
        cmds.append(f"set var controls.entered_above_[{i}] = "
                    f"{1 if pots[i] - stored > 0 else 0}")
    return cmds


def park_current_page():
    """Park all six knobs of whichever page is showing, against what the model
    already holds.

    Needed before testing anything remote: a picked-up knob is rewritten from
    its pot every audio block, so it overwrites a MIDI change within a
    millisecond. That is the pedal working as designed - the hand beats the
    controller - but it makes a map test look broken."""
    out = _run(['printf "PG %d %.4f %.4f %.4f %.4f %.4f %.4f\\n", state.page_, '
                'hw.knob[0].val_, hw.knob[1].val_, hw.knob[2].val_, '
                'hw.knob[3].val_, hw.knob[4].val_, hw.knob[5].val_'])
    page, pots = None, None
    for line in out.splitlines():
        if line.startswith("PG "):
            parts = line.split()[1:]
            page = int(parts[0])
            pots = [float(x) for x in parts[1:]]
    if page is None:
        raise RuntimeError("could not read page/pots:\n" + out)

    stored_out = _run([f'printf "SV %.4f %.4f %.4f %.4f %.4f %.4f\\n", '
                       f'state.param_[{page}][0], state.param_[{page}][1], '
                       f'state.param_[{page}][2], state.param_[{page}][3], '
                       f'state.param_[{page}][4], state.param_[{page}][5]'])
    stored = None
    for line in stored_out.splitlines():
        if line.startswith("SV "):
            stored = [float(x) for x in line.split()[1:]]
    if stored is None:
        raise RuntimeError("could not read stored values")

    cmds = []
    for i in range(6):
        cmds.append(f"set var controls.armed_[{i}] = 0")
        cmds.append(f"set var controls.entered_above_[{i}] = "
                    f"{1 if pots[i] - stored[i] > 0 else 0}")
    _run(cmds)
    return page


def setup(**kw):
    """Apply a pedal state and make it stick against soft pickup."""
    pots = read_pots()
    cmds = []

    if "bypass" in kw:
        cmds.append(f"set var state.bypassed_ = {1 if kw['bypass'] else 0}")
    for slot, name in ((SLOT_EQ, "eq_out"), (SLOT_DRIVE, "drive_out"), (SLOT_REVERB, "reverb_out")):
        if name in kw:
            cmds.append(f"set var state.slot_bypassed_[{slot}] = {1 if kw[name] else 0}")

    # Only the page currently on screen has its knobs rewritten from the pots,
    # but park against every page we touch so the test does not depend on which
    # page happens to be showing.
    for page, key in ((PAGE_EQ, "eq"), (PAGE_DRIVE, "drive"),
                      (PAGE_REVERB, "reverb"), (PAGE_META, "meta")):
        if key in kw:
            cmds += _park_cmds(pots, page, kw[key])

    return _run(cmds)


def read():
    out = _run([
        'printf "BYPASS %d\\n", state.bypassed_',
        'printf "SLOTS %d %d %d\\n", state.slot_bypassed_[0], state.slot_bypassed_[1], state.slot_bypassed_[2]',
        'printf "META %.3f %.3f %.3f %.3f %.3f %.3f\\n", state.param_[3][0],state.param_[3][1],state.param_[3][2],state.param_[3][3],state.param_[3][4],state.param_[3][5]',
        'printf "EQ %.3f %.3f %.3f %.3f %.3f %.3f\\n", state.param_[0][0],state.param_[0][1],state.param_[0][2],state.param_[0][3],state.param_[0][4],state.param_[0][5]',
        # No hw.relay_*.Read() here. Those are gdb *inferior calls*: they run
        # target code from a halted state, and with the audio interrupt live
        # they eventually wedged the MCU hard enough to need a reset. The model
        # already says what the relays have been told to do.
        'printf "PAGE %d\\n", state.page_',
    ])
    d = {}
    for line in out.splitlines():
        p = line.split()
        if p and p[0] in ("BYPASS", "SLOTS", "META", "EQ", "PAGE"):
            d[p[0]] = [float(x) for x in p[1:]]
    return d


# Neutral: everything in circuit, EQ flat, unity in and out, fully wet.
NEUTRAL = dict(
    bypass=False,
    eq_out=False, drive_out=False, reverb_out=False,
    eq=[0.5] * 6,
    meta=[1.0, 1.0, 0.5, 1.0, 0.5, 1.0],  # eqAmt rvbAmt inGain drvAmt outLvl gMix
)
