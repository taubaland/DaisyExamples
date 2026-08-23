# Project Name
TARGET = Template

# Sources
CPP_SOURCES = \
Template.cpp \
Controls.cpp \
Effect.cpp \
LedView.cpp \
PedalState.cpp

# Local headers
C_INCLUDES += -I.

# Bootloader. Leave unset (BOOT_NONE) to run straight from the 128K internal
# flash via `make program` or `make program-dfu`. Uncomment BOOT_SRAM to load
# the app into SRAM through the Daisy bootloader once you outgrow 128K --
# `make program-dfu` then needs the board in bootloader mode, and `make program`
# (openocd) is no longer available. BOOT_QSPI executes from QSPI instead: more
# room, but slower fetches, so measure before using it for DSP.
# APP_TYPE = BOOT_SRAM

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
