#!/usr/bin/env python3
"""Build the Elements sample blob, and the full flash image that carries it.

    samples.py blob <sample_data.raw> <noise_sample.raw> <noise_offset> <out>
    samples.py join <program.bin> <samples.bin> <image_offset> <out>

`blob` lays the two tables out the way Samples.cpp expects to find them --
the first at 0, the second at <noise_offset> -- behind a 16-byte header:

    magic 'ELSM' | version | payload size | FNV-1a of the payload

all little-endian uint32. Keep it in step with samples::Header.

`join` pads the program out to <image_offset> and appends the blob, giving
the one file that is written to QSPI at 0x90040000.
"""

import struct
import sys

MAGIC = 0x4D534C45  # 'ELSM'
VERSION = 1

# The bootloader copies at most this much of the image into RAM. A program
# bigger than this does not boot, and a blob placed below it would be
# overwritten in RAM by nothing and read from flash as program -- so both
# ends are checked here rather than discovered on the bench.
BOOTLOADER_LIMIT = 480 * 1024


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def blob(sample_path, noise_path, noise_offset, out_path):
    sample = open(sample_path, "rb").read()
    noise = open(noise_path, "rb").read()

    if not sample or not noise:
        sys.exit("samples.py: a table came out empty -- has the section "
                 "name in the Makefile drifted from the compiler's?")
    if len(sample) > noise_offset:
        sys.exit("samples.py: sample table (%d B) overruns the noise "
                 "table's offset (%d)" % (len(sample), noise_offset))

    payload = sample + bytes(noise_offset - len(sample)) + noise
    header = struct.pack("<4I", MAGIC, VERSION, len(payload), fnv1a(payload))

    open(out_path, "wb").write(header + payload)
    print("samples.py: %s, %d B payload" % (out_path, len(payload)))


def join(program_path, samples_path, image_offset, out_path):
    program = open(program_path, "rb").read()
    samples = open(samples_path, "rb").read()

    if len(program) > BOOTLOADER_LIMIT:
        sys.exit("samples.py: program is %d B, over the %d B the bootloader "
                 "will load" % (len(program), BOOTLOADER_LIMIT))
    if image_offset < BOOTLOADER_LIMIT:
        sys.exit("samples.py: blob offset 0x%X is inside the region the "
                 "bootloader loads" % image_offset)

    # 0xFF, so the gap is left as erased flash rather than programmed.
    image = program + b"\xff" * (image_offset - len(program)) + samples
    open(out_path, "wb").write(image)
    print("samples.py: %s, %d B (program %d B, %.0f%% of what the bootloader "
          "will load)" % (out_path, len(image), len(program),
                          100.0 * len(program) / BOOTLOADER_LIMIT))


def main(argv):
    if len(argv) == 6 and argv[1] == "blob":
        blob(argv[2], argv[3], int(argv[4], 0), argv[5])
    elif len(argv) == 6 and argv[1] == "join":
        join(argv[2], argv[3], int(argv[4], 0), argv[5])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
