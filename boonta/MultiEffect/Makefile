# Project Name
TARGET = MultiEffect

# Sources
CPP_SOURCES = \
MultiEffect.cpp \
Chain.cpp \
Controls.cpp \
DriveEffect.cpp \
EffectRegistry.cpp \
EqEffect.cpp \
GenEffect.cpp \
LedView.cpp \
MidiControl.cpp \
PedalState.cpp \
ReverbEffect.cpp \
Storage.cpp \
Sysex.cpp \
UsbDiag.cpp

# Local headers
C_INCLUDES += -I.

# The RNBO patches. Point at the directory the exports sit in, one
# subdirectory per patch:
#
#   make RNBO_DIR=/d/Projects/rnbo
#
# They go in together or not at all. Dropping one would renumber the effect ids
# after it and every saved preset would come back as the wrong effect, so the
# list below is the build's contract -- adding a patch means appending to it,
# never inserting. Each needs an adapter of its own naming its knobs; see
# RnboPitchshifter.cpp for the shape of one.
RNBO_PATCHES = pitchshifter octaver freezer

# RNBO_NOTHROW because the default platform throws and this builds
# -fno-exceptions; RNBO_USE_FLOAT32 because otherwise a SampleValue is a double,
# which costs both arithmetic and a conversion pass over every block;
# RNBO_USECUSTOMALLOCATOR because the patches' buffers do not fit in the heap
# and RnboHeap.cpp puts them in SDRAM instead. See RnboEffect.h for the rest.
ifneq ($(RNBO_DIR),)
# Each export's own directory, for its rnbo.<name>.cpp ...
C_INCLUDES += $(foreach p,$(RNBO_PATCHES),-I$(RNBO_DIR)/$(p))
# ... and one copy of the RNBO library, which every export carries a
# byte-identical duplicate of. Taking it from one of them keeps a single
# definition of the library types, which is what lets the linker fold the
# three patches' shared machinery into one.
RNBO_LIB = $(RNBO_DIR)/$(firstword $(RNBO_PATCHES))/rnbo
C_INCLUDES += -I$(RNBO_LIB) -I$(RNBO_LIB)/common -I$(RNBO_LIB)/src
C_DEFS += -DRNBO_NOTHROW -DRNBO_USE_FLOAT32 -DRNBO_USECUSTOMALLOCATOR -DBOONTA_RNBO
CPP_SOURCES += RnboHeap.cpp
CPP_SOURCES += RnboPitchshifter.cpp RnboOctaver.cpp RnboFreezer.cpp
endif

# Uncomment to time the audio callback into a global an ST-Link can read. Costs
# a couple of microseconds a block and is off by default; see the README.
C_DEFS += -DPROFILE_CPU

# Uncomment to record peak levels either side of the DSP into a global an
# ST-Link can read, for finding out which side of the codec a signal problem is
# on. See the README.
# C_DEFS += -DPROBE_IO

# Bootloader. The app outgrew the comfortable part of the 128K internal flash:
# with MIDI, presets and UsbDiag it reached 91.9%, about 8K of headroom, and the
# libDaisy upstream merge alone cost 380 bytes of that. BOOT_SRAM runs the app
# from SRAM instead -- the same build is 24.5% of 480K -- and leaves internal
# flash to the bootloader. The reverb tank is half a megabyte but it lives in
# SDRAM, which no app type touches.
#
# What this changes:
#
#   make program      gone; openocd cannot write QSPI on this board
#   make program-dfu  writes the app into QSPI at 0x90040000
#   make sram         loads straight into SRAM over the ST-Link, no QSPI write
#   make boot         puts the bootloader in internal flash, over the ST-Link
#
# Clearing APP_TYPE returns to internal flash, but remember the bootloader is
# sitting there: `make boot` and `make program` are two ways of owning the same
# 128K, and whichever ran last wins.
APP_TYPE = BOOT_SRAM

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Force unsuffixed floating-point literals to float in the RNBO translation
# units only:
#
#   make RNBO_DIR=/d/Projects/rnbo RNBO_SINGLE_PRECISION=1
#
# RNBO_USE_FLOAT32 makes `number` a float, but the generated code still writes
# its constants as doubles -- a grain window comes out as
# `0.5 * (1 - rnbo_cos(6.28318530717958647692 * x))`, and that literal promotes
# the whole expression, so rnbo_cos picks its double overload and every sample
# goes through libm. Measured at 10.75% of the freezer's time. This flag makes
# those literals float so the float overloads win.
#
# It goes on all three patches or none. The RNBO library is instantiated in
# every one of them and its member functions have vague linkage, so the linker
# folds the three copies into one -- and if they were not compiled alike, which
# copy survives would be arbitrary. That is also why this cannot be narrowed to
# the freezer alone.
#
# Off by default because it changes the meaning of every constant in those
# files, not only the ones that wanted to be float.
ifeq ($(RNBO_SINGLE_PRECISION),1)
$(BUILD_DIR)/RnboPitchshifter.o $(BUILD_DIR)/RnboOctaver.o $(BUILD_DIR)/RnboFreezer.o: CPPFLAGS += -fsingle-precision-constant
endif

# Host-side DSP tests. Deliberately after the include above so that libDaisy's
# `all` stays the default goal.
.PHONY: test
test:
	$(MAKE) -C test run

# Entry point of the image about to be loaded. Read from the ELF rather than
# hardcoded, because it moves whenever a change shifts Reset_Handler.
SRAM_ENTRY = $(shell $(PREFIX)readelf -h $(BUILD_DIR)/$(TARGET).elf 2>/dev/null | awk '/Entry point/ {print $$NF}')

# Load into SRAM over the ST-Link: about a second, no flash wear, and gone on
# the next power cycle. Needs the bootloader in internal flash and some valid
# app already in QSPI -- see sram-load.cfg for why it has to catch the
# bootloader's handover rather than staging the jump itself.
.PHONY: sram
sram: all
	@echo "loading $(BUILD_DIR)/$(TARGET).elf into SRAM, entry $(SRAM_ENTRY)"
	@$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "set APP_ELF $(BUILD_DIR)/$(TARGET).elf; \
		    set APP_ENTRY $(SRAM_ENTRY); \
		    unset -nocomplain _" \
		-f sram-load.cfg

# The bootloader, over the ST-Link rather than DFU. libDaisy's `program-boot`
# needs the chip already in DFU mode, which is a chicken-and-egg problem on a
# board whose app has stopped booting. openocd has no such requirement.
.PHONY: boot
boot:
	$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "program $(BOOT_BIN) 0x08000000 verify reset exit"
