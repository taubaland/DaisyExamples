#!/bin/sh
# Get a build onto the pedal. The equivalent of libDaisy's `make program`,
# `program-dfu`, `program-boot` and MultiEffect's `make sram`, since arduino-cli
# only knows how to upload over its own configured programmer.
#
#   ./flash.sh boot         # Daisy bootloader -> internal flash (one-time)
#   ./flash.sh BOOT_NONE    # app -> internal flash, over the ST-Link
#   ./flash.sh BOOT_QSPI    # app -> QSPI at 0x90040000, over the ST-Link
#   ./flash.sh BOOT_SRAM    # app -> QSPI at 0x90040000, over the ST-Link
#   ./flash.sh sram         # app -> straight into SRAM, over the ST-Link
#
# Everything goes over the ST-Link, and nothing needs a button held. That is
# not the usual Daisy arrangement: QSPI is normally written over DFU, and DFU
# does not work on this board at all -- the Daisy bootloader never enumerates,
# because libDaisy's USB stack does not come up here. See PORTING.md, "Why
# BOOT_QSPI has not run".
#
# The two QSPI actions instead run ../QspiWriter, a flash loader that writes
# the chip from the target side. Set USE_DFU=1 to take the DFU route anyway,
# on a board where it works; that needs the chip already in DFU mode (hold
# BOOT, tap RESET, release BOOT).
#
# Build first -- this only flashes what build.sh left in build/<APP_TYPE>/.

set -e

ACTION="${1:-BOOT_NONE}"

SKETCH_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SKETCH_DIR/../.." && pwd)

OCD="${OCD:-/c/Program Files/DaisyToolchain/bin/openocd.exe}"
OCD_DIR="${OCD_DIR:-/c/Program Files/DaisyToolchain/openocd/scripts}"
OCDFLAGS="-f interface/stlink.cfg -f target/stm32h7x.cfg"
DFU_UTIL="${DFU_UTIL:-/c/Program Files/DaisyToolchain/bin/dfu-util.exe}"
READELF="${READELF:-$HOME/AppData/Local/Arduino15/packages/STMicroelectronics/tools/xpack-arm-none-eabi-gcc/14.2.1-1.1/bin/arm-none-eabi-readelf.exe}"

# v6.4 with the internal-DFU 2000ms window, the same default libDaisy's
# core/Makefile uses.
BOOT_BIN="$REPO_ROOT/libDaisy/core/dsy_bootloader_v6_4-intdfu-2000ms.bin"

# openocd is a native Windows binary, so a path inside a -c Tcl string has to be
# a Windows path. Arguments in their own argv slot get converted by the shell;
# anything embedded in a quoted command does not, and openocd just reports
# "couldn.t open". Same class of bug as the unquoted OCD_DIR in libDaisy.
winpath() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi
}

need() {
    [ -f "$1" ] || { echo "no $1 -- run ./build.sh $2 first" >&2; exit 1; }
}

case "$ACTION" in
    boot)
        # Over the ST-Link rather than DFU: libDaisy's `program-boot` needs the
        # chip already in DFU mode, which is a chicken-and-egg problem on a
        # board whose app has stopped booting.
        "$OCD" -s "$OCD_DIR" $OCDFLAGS \
            -c "program $(winpath "$BOOT_BIN") 0x08000000 verify reset exit"
        ;;

    BOOT_NONE)
        BIN="$SKETCH_DIR/build/BOOT_NONE/NewEffectDuino.ino.bin"
        need "$BIN" BOOT_NONE
        # Overwrites the bootloader, if one is installed. ./flash.sh boot puts
        # it back.
        "$OCD" -s "$OCD_DIR" $OCDFLAGS \
            -c "program $(winpath "$BIN") 0x08000000 verify reset exit"
        ;;

    BOOT_SRAM|BOOT_QSPI)
        BIN="$SKETCH_DIR/build/$ACTION/NewEffectDuino.ino.bin"
        need "$BIN" "$ACTION"
        # Both live in QSPI at 0x90040000. The difference is what the
        # bootloader does with it: a BOOT_SRAM image is copied into AXI SRAM and
        # run from there, a BOOT_QSPI one is executed in place. The bootloader
        # checks the image before jumping and runs it where its vectors point,
        # so the same command serves both.
        if [ "${USE_DFU:-0}" = "1" ]; then
            "$DFU_UTIL" -a 0 -s 0x90040000:leave -D "$BIN" -d ,0483:df11
        else
            "$REPO_ROOT/boonta/QspiWriter/qspi-write.sh" "$BIN" 0x90040000
        fi
        ;;

    sram)
        ELF="$SKETCH_DIR/build/BOOT_SRAM/NewEffectDuino.ino.elf"
        need "$ELF" BOOT_SRAM
        # Entry point read from the ELF rather than hardcoded, because it moves
        # whenever a change shifts Reset_Handler.
        ENTRY=$("$READELF" -h "$ELF" | awk '/Entry point/ {print $NF}')
        echo "loading $ELF into SRAM, entry $ENTRY"
        "$OCD" -s "$OCD_DIR" $OCDFLAGS \
            -c "set APP_ELF $(winpath "$ELF"); set APP_ENTRY $ENTRY; unset -nocomplain _" \
            -f "$SKETCH_DIR/sram-load.cfg"
        ;;

    *)
        echo "unknown action \"$ACTION\" (boot, BOOT_NONE, BOOT_SRAM, BOOT_QSPI, sram)" >&2
        exit 1
        ;;
esac
