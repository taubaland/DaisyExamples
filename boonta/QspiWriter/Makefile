# Project Name
TARGET = QspiWriter

# Sources
CPP_SOURCES = QspiWriter.cpp

# Local headers
C_INCLUDES += -I.

# BOOT_SRAM is not a preference here, it is the whole point: a program cannot
# write the flash chip it is executing from, and QSPIHandle refuses to try
# (CheckProgramMemory -> E_INVALID_MODE). Building this BOOT_QSPI would produce
# something that cannot do its job; BOOT_NONE would mean overwriting the
# bootloader in internal flash to install the tool that exists to avoid
# overwriting things.
APP_TYPE = BOOT_SRAM

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Write a file into QSPI:
#
#   make write FILE=../NewEffectDuino/build/BOOT_QSPI/NewEffectDuino.ino.bin
#   make write FILE=... ADDR=0x90040000
#
# ADDR defaults to the address the Daisy bootloader looks for an app at. See
# qspi-write.sh, which is what this calls and what the script in
# ../NewEffectDuino/flash.sh calls too.
.PHONY: write
write: all
	@./qspi-write.sh $(FILE) $(ADDR)
