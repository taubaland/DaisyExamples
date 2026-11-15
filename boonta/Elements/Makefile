# Project Name
TARGET = Elements

# Sources
CPP_SOURCES = \
Elements.cpp \
Controls.cpp \
Effect.cpp \
ElementsDsp.cpp \
ElementsResources.cpp \
LedView.cpp \
MidiControl.cpp \
PedalState.cpp \
Persist.cpp \
Samples.cpp \
UsbDiag.cpp

# Local headers, which is also where the vendored `elements/` tree resolves
# from, and the top of the repository, which is where `stmlib/` does.
C_INCLUDES += -I. -I../..

# Time the audio callback into a global an ST-Link can read. Costs a couple of
# microseconds a block; see the README for how to read it.
C_DEFS += -DPROFILE_CPU

# Bootloader. Runs from SRAM through the Daisy bootloader:
#
#   make              build/Elements-full.bin: the program and the samples
#   make boot         put the bootloader in internal flash, over the ST-Link
#   make program-dfu  write the *program only* into QSPI -- see the README
#   make sram         load straight into SRAM over the ST-Link, no QSPI write
#   make sram-bare    the same, on a board with no bootloader at all
APP_TYPE = BOOT_SRAM

# --- the sample tables ------------------------------------------------------
#
# Elements' two sample tables do not travel in the program; Samples.h says
# why. Three numbers describe where they go instead, and each has to agree
# with its twin in Samples.h:
#
#   SAMPLES_SDRAM         where Samples::Load() copies them, and so where the
#                         DSP is linked to find them: the last megabyte of the
#                         64 MB SDRAM, far above anything .sdram_bss reaches
#   SAMPLES_NOISE_OFFSET  the second table's offset from the first
#   SAMPLES_IMAGE_OFFSET  where the blob sits in Elements-full.bin: 512 KB in,
#                         past the 480 KB the bootloader will copy into RAM
SAMPLES_SDRAM        = 0xC3F00000
SAMPLES_NOISE_OFFSET = 0x40000
SAMPLES_IMAGE_OFFSET = 0x80000

SAMPLES_NOISE_SDRAM = $(shell printf '0x%X' $$(($(SAMPLES_SDRAM) + $(SAMPLES_NOISE_OFFSET))))

# The names the DSP refers to, given addresses instead of definitions.
LDFLAGS += -Wl,--defsym=_ZN8elements15smp_sample_dataE=$(SAMPLES_SDRAM)
LDFLAGS += -Wl,--defsym=_ZN8elements16smp_noise_sampleE=$(SAMPLES_NOISE_SDRAM)

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

PYTHON ?= python

# The blob: the two tables as ElementsResources.cpp compiled them (renamed,
# so the linker left them out of the program), cut out of its object file and
# given a header Samples::Load() can check.
SAMPLES_BIN = $(BUILD_DIR)/samples.bin
FULL_BIN    = $(BUILD_DIR)/$(TARGET)-full.bin

$(SAMPLES_BIN): $(BUILD_DIR)/ElementsResources.o tools/samples.py
	$(CP) -O binary -j .rodata._ZN8elements19smp_sample_data_romE $< $(BUILD_DIR)/smp_sample_data.raw
	$(CP) -O binary -j .rodata._ZN8elements20smp_noise_sample_romE $< $(BUILD_DIR)/smp_noise_sample.raw
	$(PYTHON) tools/samples.py blob $(BUILD_DIR)/smp_sample_data.raw \
		$(BUILD_DIR)/smp_noise_sample.raw $(SAMPLES_NOISE_OFFSET) $@

# The one file to write to QSPI at 0x90040000: the program, padding, the blob.
$(FULL_BIN): $(BUILD_DIR)/$(TARGET).bin $(SAMPLES_BIN) tools/samples.py
	$(PYTHON) tools/samples.py join $(BUILD_DIR)/$(TARGET).bin $(SAMPLES_BIN) \
		$(SAMPLES_IMAGE_OFFSET) $@

all: $(FULL_BIN)

# Host-side DSP tests.
.PHONY: test
test:
	$(MAKE) -C test run

# Entry point of the image about to be loaded. Read from the ELF rather than
# hardcoded, because it moves whenever a change shifts Reset_Handler.
SRAM_ENTRY = $(shell $(PREFIX)readelf -h $(BUILD_DIR)/$(TARGET).elf 2>/dev/null | awk '/Entry point/ {print $$NF}')

# Where samples::loaded lives, for sram-bare to set once it has put the
# tables in SDRAM itself.
SAMPLES_LOADED = 0x$(shell $(PREFIX)nm $(BUILD_DIR)/$(TARGET).elf 2>/dev/null | awk '/ _ZN7samples6loadedE$$/ {print $$1}')

# Load into SRAM over the ST-Link: about a second, no flash wear, and gone on
# the next power cycle. Needs the bootloader in internal flash and some valid
# app already in QSPI -- see sram-load.cfg. The sample tables come from QSPI
# as usual, so they have to have been written there once (Elements-full.bin).
.PHONY: sram
sram: all
	@echo "loading $(BUILD_DIR)/$(TARGET).elf into SRAM, entry $(SRAM_ENTRY)"
	@$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "set APP_ELF $(BUILD_DIR)/$(TARGET).elf; \
		    set APP_ENTRY $(SRAM_ENTRY); \
		    unset -nocomplain _" \
		-f sram-load.cfg

# The same, for a board with no bootloader in internal flash and nothing of
# ours in QSPI: openocd stands in for the bootloader, and puts the sample
# tables into SDRAM by hand once the program has brought SDRAM up. Writes no
# flash of any kind. See sram-bare.cfg.
.PHONY: sram-bare
sram-bare: all
	@echo "staging $(BUILD_DIR)/$(TARGET).elf into SRAM with no bootloader"
	@$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "set APP_ELF $(BUILD_DIR)/$(TARGET).elf; \
		    set SAMPLES_BIN $(SAMPLES_BIN); \
		    set SAMPLES_SDRAM $(SAMPLES_SDRAM); \
		    set SAMPLES_LOADED $(SAMPLES_LOADED); \
		    unset -nocomplain _" \
		-f sram-bare.cfg

# The bootloader, over the ST-Link rather than DFU.
.PHONY: boot
boot:
	$(OCD) -s $(OCD_DIR) $(OCDFLAGS) \
		-c "program $(BOOT_BIN) 0x08000000 verify reset exit"
