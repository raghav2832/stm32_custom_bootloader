#!/usr/bin/env python3
"""
patch_header.py

Patches image_size, crc32, and version into an application's compiled
.bin, matching the layout of app_header_t in app_header.h:

    offset 0  : magic      (4 bytes)
    offset 4  : image_size (4 bytes)
    offset 8  : crc32      (4 bytes)
    offset 12 : version    (4 bytes)
"""

import sys
import os
import struct
import zlib

HEADER_MAGIC = 0xB00710AD
SIZE_OFFSET = 4
CRC_OFFSET = 8
VERSION_OFFSET = 12


def get_and_bump_version():
    project_root = os.path.dirname(os.getcwd())
    version_file = os.path.join(project_root, "version.txt")

    if os.path.exists(version_file):
        with open(version_file, "r") as f:
            version = int(f.read().strip())
    else:
        version = 0

    version += 1

    with open(version_file, "w") as f:
        f.write(str(version))

    return version


def patch_header(bin_path):
    with open(bin_path, "rb") as f:
        data = bytearray(f.read())

    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != HEADER_MAGIC:
        sys.exit(f"error: {bin_path} does not start with APP_HEADER_MAGIC "
                  f"(found 0x{magic:08X}, expected 0x{HEADER_MAGIC:08X})")

    image_size = len(data)
    struct.pack_into("<I", data, SIZE_OFFSET, image_size)

    version = get_and_bump_version()
    struct.pack_into("<I", data, VERSION_OFFSET, version)

    struct.pack_into("<I", data, CRC_OFFSET, 0)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    struct.pack_into("<I", data, CRC_OFFSET, crc)

    with open(bin_path, "wb") as f:
        f.write(data)

    print(f"{bin_path}: size={image_size} bytes, version={version}, crc32=0x{crc:08X}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: patch_header.py <path-to-bin>")
    patch_header(sys.argv[1])