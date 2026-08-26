# Project Name
TARGET = MultiEffect

# Sources
CPP_SOURCES = \
MultiEffect.cpp \
Chain.cpp \
Controls.cpp \
DriveEffect.cpp \
EqEffect.cpp \
LedView.cpp \
MidiControl.cpp \
PedalState.cpp \
ReverbEffect.cpp \
Storage.cpp

# Local headers
C_INCLUDES += -I.

# Uncomment to time the audio callback into a global an ST-Link can read. Costs
# a couple of microseconds a block and is off by default; see the README.
# C_DEFS += -DPROFILE_CPU

# Uncomment to record peak levels either side of the DSP into a global an
# ST-Link can read, for finding out which side of the codec a signal problem is
# on. See the README.
# C_DEFS += -DPROBE_IO

# Bootloader. All three effects fit in the 128K internal flash with room to
# spare (101012 B, 77%), so this is left unset and `make program` / `make
# program-dfu` work as they do for the Template. The reverb tank is half a
# megabyte but it lives in SDRAM, which no app type touches.
#
# Uncomment BOOT_SRAM if you outgrow the remaining 27K -- `make program-dfu`
# then needs the board in bootloader mode, and `make program` (openocd) is no
# longer available. BOOT_QSPI works too but fetches slower than SRAM, and the
# reverb is already the busiest thing here.
# APP_TYPE = BOOT_SRAM

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Host-side DSP tests. Deliberately after the include above so that libDaisy's
# `all` stays the default goal.
.PHONY: test
test:
	$(MAKE) -C test run
