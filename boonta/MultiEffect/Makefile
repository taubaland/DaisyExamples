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
UsbDiag.cpp

# Local headers
C_INCLUDES += -I.

# Uncomment to time the audio callback into a global an ST-Link can read. Costs
# a couple of microseconds a block and is off by default; see the README.
# C_DEFS += -DPROFILE_CPU

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
