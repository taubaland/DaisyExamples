#!/bin/sh
# Build the sketch for one of the three app types, the way libDaisy's
# core/Makefile does with APP_TYPE. arduino-cli has no equivalent, so the
# linker script and the SystemInit override are passed in as build properties.
#
#   ./build.sh              # BOOT_NONE, the default
#   ./build.sh BOOT_SRAM
#   ./build.sh BOOT_QSPI
#
# Output lands in build/<APP_TYPE>/. Flashing is in PORTING.md.

set -e

APP_TYPE="${1:-BOOT_NONE}"
FQBN="STMicroelectronics:stm32:GenH7:pnum=DAISY_SEED"

SKETCH_DIR=$(cd "$(dirname "$0")" && pwd)
ARDUINO_CLI="${ARDUINO_CLI:-/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe}"

case "$APP_TYPE" in
    BOOT_NONE)
        # Stock DAISY_SEED.ld: internal flash at 0x08000000, 128K.
        EXTRA_CFLAGS=""
        EXTRA_LDFLAGS=""
        ;;
    BOOT_SRAM)
        EXTRA_CFLAGS="-DBOOT_APP"
        EXTRA_LDFLAGS="-Wl,--default-script=$SKETCH_DIR/boot_sram.ld -Wl,--wrap=SystemInit"
        ;;
    BOOT_QSPI)
        EXTRA_CFLAGS="-DBOOT_APP"
        EXTRA_LDFLAGS="-Wl,--default-script=$SKETCH_DIR/boot_qspi.ld -Wl,--wrap=SystemInit"
        ;;
    *)
        echo "unknown app type \"$APP_TYPE\" (BOOT_NONE, BOOT_SRAM or BOOT_QSPI)" >&2
        exit 1
        ;;
esac

# --default-script is what the core's recipe uses for the variant script, and
# the last one on the command line wins -- compiler.c.elf.extra_flags is
# substituted after it. --wrap redirects the startup code's call to
# SystemInit(); see BootApp.cpp.
exec "$ARDUINO_CLI" compile \
    --fqbn "$FQBN" \
    --build-property "build.extra_flags=$EXTRA_CFLAGS" \
    --build-property "compiler.c.elf.extra_flags=$EXTRA_LDFLAGS" \
    --output-dir "$SKETCH_DIR/build/$APP_TYPE" \
    "$SKETCH_DIR"
