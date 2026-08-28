# Hardware measurement rig for the Boonta MultiEffect.
#
# Unlike ../test, which is pure host DSP and needs nothing, this drives the real
# pedal. It needs three things running:
#
#   1. An audio interface with the pedal's OUTPUT patched to one of its inputs.
#      Set BOONTA_PEDAL_OUT_CH to which one (zero-based, default 1).
#   2. A source into the pedal's INPUT. A guitar is fine - the measurements are
#      relative, so a wandering stimulus is averaged out by interleaving.
#   3. openocd serving gdb on :3333, and a PROBE_IO build on the board:
#
#        openocd -s <scripts> -f interface/stlink.cfg -f target/stm32h7x.cfg
#
#      Uncomment `C_DEFS += -DPROBE_IO` in ../Makefile, rebuild and reflash, or
#      the scripts that read codec levels will tell you the symbol is missing.
#
#   make venv       create the virtualenv and install numpy + sounddevice
#   make interface  list devices, show what opens, scan the inputs
#   make listen     what is arriving at the interface and at the codec
#   make verify     regression check on the relay logic (engaged vs bypassed)
#   make dsp        A/B the EQ and drive at the pedal output
#   make response   measure one EQ band's gain at the note you are playing
#                   (BAND=low|mid|high, default mid)
#   make relays     sweep all eight relay states, measuring both ends
#   make persist    check settings survive a reset (needs no audio at all)
#   make midi       send CCs and check the model moved. PORT="name" to choose
#                   the port; use `make midiprobe` first if you do not know it
#   make midiprobe  find which MIDI output actually reaches the pedal
#   make loopcheck  is the monitoring path feeding the pedal back into itself?
#                   run this FIRST for any complaint about how it sounds

PY = venv/Scripts/python.exe
ifeq ($(OS),)
PY = venv/bin/python
endif

.PHONY: venv interface listen verify dsp response relays persist midi midiprobe loopcheck clean

BAND ?= mid

venv: $(PY)

$(PY):
	python -m venv venv
	$(PY) -m pip install --quiet --upgrade pip
	$(PY) -m pip install --quiet -r requirements.txt

interface: $(PY)
	@$(PY) interface.py

listen: $(PY)
	@$(PY) listen.py

verify: $(PY)
	@$(PY) verify.py

dsp: $(PY)
	@$(PY) dsp.py

response: $(PY)
	@$(PY) response.py $(BAND)

relays: $(PY)
	@$(PY) relays.py

persist: $(PY)
	@$(PY) persist.py

midi: $(PY)
	@$(PY) midi_test.py $(PORT)

midiprobe: $(PY)
	@$(PY) midi_probe.py

loopcheck: $(PY)
	@$(PY) loopcheck.py

clean:
	rm -rf venv __pycache__
