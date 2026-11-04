#!/bin/sh
# Write a file into the Boonta's QSPI flash, over the ST-Link.
#
#   ./qspi-write.sh <file> [address]
#
# address defaults to 0x90040000, where the Daisy bootloader looks for an app.
#
# This is the host half of QspiWriter: it builds the writer if it is not built,
# works out where the mailbox and the breakpoint symbol ended up, splits the
# payload into chunks the writer's buffer can hold, and hands the lot to
# openocd. See QspiWriter.cpp for why a target-side writer is the only way into
# QSPI on this board.
#
# Needs, as every route onto this board does, the Daisy bootloader in internal
# flash and some valid app already in QSPI -- the writer gets loaded by
# catching the bootloader's handover. ../NewEffectDuino/flash.sh boot installs
# the bootloader.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)

FILE="$1"
DEST="${2:-0x90040000}"

if [ -z "$FILE" ]; then
    echo "usage: $0 <file> [address]" >&2
    exit 1
fi
[ -f "$FILE" ] || { echo "no such file: $FILE" >&2; exit 1; }

OCD="${OCD:-/c/Program Files/DaisyToolchain/bin/openocd.exe}"
OCD_DIR="${OCD_DIR:-/c/Program Files/DaisyToolchain/openocd/scripts}"
OCDFLAGS="-f interface/stlink.cfg -f target/stm32h7x.cfg"
PREFIX="${PREFIX:-arm-none-eabi-}"

# Must match kChunkBytes in QspiWriter.h. The Tcl script is told, rather than
# told twice.
CHUNK=32768

# openocd is a native Windows binary, so a path inside a -c Tcl string, or in a
# -f argument, has to be a Windows path -- MSYS converts arguments in their own
# argv slot and nothing else, and openocd reports the difference only as
# "couldn.t open".
winpath() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi
}

ELF="$HERE/build/QspiWriter.elf"
if [ ! -f "$ELF" ]; then
    echo "building the writer first"
    make -C "$HERE"
fi

# Every address the Tcl script needs, read out of the ELF rather than written
# down anywhere: all three move whenever the writer is rebuilt.
ENTRY=$("${PREFIX}readelf" -h "$ELF" | awk '/Entry point/ {print $NF}')
sym() {
    addr=$("${PREFIX}nm" "$ELF" | awk -v s="$1" '$3 == s {print "0x" $1}')
    [ -n "$addr" ] || { echo "no symbol $1 in $ELF" >&2; exit 1; }
    echo "$addr"
}
MB=$(sym g_mailbox)
RDY=$(sym QspiWriterReady)

SIZE=$(wc -c < "$FILE" | tr -d ' ')
# Erase by whole 4K sectors; asking for fewer bytes than that would leave the
# tail of the last sector holding whatever was there before.
SPAN=$(( (SIZE + 4095) / 4096 * 4096 ))

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
split -b "$CHUNK" -d -a 3 "$FILE" "$WORK/chunk"
NCHUNKS=$(ls "$WORK"/chunk* | wc -l | tr -d ' ')

echo "$FILE: $SIZE bytes -> $DEST ($NCHUNKS chunks, erasing $SPAN)"

"$OCD" -s "$OCD_DIR" $OCDFLAGS \
    -c "set WRITER_ELF {$(winpath "$ELF")}; \
        set WRITER_ENTRY $ENTRY; \
        set MB $MB; \
        set RDY $RDY; \
        set DEST $DEST; \
        set SPAN $SPAN; \
        set CHUNK $CHUNK; \
        set NCHUNKS $NCHUNKS; \
        set CHUNK_FMT {$(winpath "$WORK")/chunk%03d}; \
        unset -nocomplain _" \
    -f "$(winpath "$HERE/qspi-write.cfg")"
