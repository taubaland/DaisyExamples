# Project Name
TARGET = Clouds

# Sources
CPP_SOURCES = \
Clouds.cpp \
Controls.cpp \
Effect.cpp \
LedView.cpp \
MidiControl.cpp \
PedalState.cpp \
Reverb.cpp \
UsbDiag.cpp

# Local headers
C_INCLUDES += -I.

# Time the audio callback into a global an ST-Link can read. Costs a couple of
# microseconds a block; see the README for the gdb one-liner that reads it.
# Left on because a granulator's cost depends entirely on how it is set, so
# the number is worth having to hand.
C_DEFS += -DPROFILE_CPU

# Bootloader. The grain engine, the plate and USB MIDI together no longer fit
# the 128K internal flash, so this runs from SRAM through the Daisy
# bootloader:
#
#   make boot         put the bootloader in internal flash, over the ST-Link
#   make program-dfu  write the app into QSPI, board in bootloader mode
#   make sram         load straight into SRAM over the ST-Link, no QSPI write
#
# `make program` (openocd straight to internal flash) is refused for boot app
# types. The two megabyte recording buffer and the reverb tank are in SDRAM
# either way: no app type touches it, and no app type could hold it.
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
