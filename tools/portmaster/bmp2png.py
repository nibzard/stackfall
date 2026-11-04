#!/usr/bin/env python3
"""Convert a checkpoint BMP of STACKFALL into a PNG.

Usage: bmp2png.py <input.bmp> <output.png>

The screenshot capture of the release package writes one BMP per
checkpoint command. PortMaster requires a PNG, so this converts it with
the Python standard library alone: no image package is installed on the
build host, and the package must build from a clean tree.

It reads the forms the checkpoint writer produces (docs/AGENT_TESTING.md,
the bounded-image pass): 8-bit palettized frames, compressed with BI_RLE8
or stored uncompressed, and the 24-bit uncompressed escape. It writes a
non-interlaced 8-bit truecolor PNG with no extra chunks, so the same input
always produces the same output file.
"""

import struct
import sys
import zlib


def fail(message):
    print(f"bmp2png: {message}", file=sys.stderr)
    raise SystemExit(1)


def decode_rle8(data, offset, width, height, size):
    """The pixel indexes of one BI_RLE8 stream, top row first."""
    indexes = bytearray(width * height)
    x = 0
    y = height - 1  # the writer stores rows bottom-up
    pos = offset
    while True:
        if pos + 2 > size:
            fail("truncated RLE8 stream")
        count, value = data[pos], data[pos + 1]
        pos += 2
        if count == 0:
            if value == 0:  # end of line
                x = 0
                y -= 1
                continue
            if value == 1:  # end of bitmap
                break
            if value == 2:  # delta
                if pos + 2 > size:
                    fail("truncated RLE8 delta")
                x += data[pos]
                y -= data[pos + 1]
                pos += 2
                if x > width or y < 0:
                    fail("RLE8 delta off the canvas")
                continue
            # An absolute run of literal palette indexes, word aligned.
            run = value
            if run < 3 or pos + run > size:
                fail("bad absolute RLE8 run")
            for step in range(run):
                if y >= 0 and x < width:
                    indexes[y * width + x] = data[pos + step]
                    x += 1
            pos += run + (run % 2)
            continue
        if y < 0 or x + count > width:
            fail("RLE8 run past the row or canvas edge")
        for _ in range(count):
            indexes[y * width + x] = value
            x += 1
    return indexes


def read_bmp(data):
    """One BMP as (width, height, rows of RGB tuples, top row first)."""
    size = len(data)
    if size < 54 or data[:2] != b"BM":
        fail("not a BMP file")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, raw_height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or raw_height <= 0:
        fail(f"bad size {width}x{raw_height}")
    height = raw_height  # the writer stores rows bottom-up

    if depth == 8:
        colors_used = struct.unpack_from("<I", data, 46)[0] or 256
        palette_base = 14 + 40
        if size < palette_base + colors_used * 4:
            fail("truncated palette")
        palette = [
            (data[palette_base + i * 4 + 2], data[palette_base + i * 4 + 1],
             data[palette_base + i * 4])
            for i in range(colors_used)
        ]
        if compression == 1:  # BI_RLE8
            indexes = decode_rle8(data, offset, width, height, size)
        elif compression == 0:  # BI_RGB
            stride = (width + 3) // 4 * 4
            if size < offset + stride * height:
                fail("truncated 8-bit body")
            indexes = bytearray(width * height)
            for y in range(height):
                source = offset + y * stride
                target = (height - 1 - y) * width
                indexes[target:target + width] = data[source:source + width]
        else:
            fail(f"unsupported 8-bit compression {compression}")
        rows = []
        for y in range(height):
            base = y * width
            rows.append([palette[i] for i in indexes[base:base + width]])
        return width, height, rows

    if depth == 24 and compression == 0:
        stride = (width * 3 + 3) // 4 * 4
        if size < offset + stride * height:
            fail("truncated 24-bit body")
        rows = []
        for y in range(height):
            base = offset + (height - 1 - y) * stride
            rows.append([(data[base + x * 3 + 2], data[base + x * 3 + 1],
                          data[base + x * 3]) for x in range(width)])
        return width, height, rows

    fail(f"unsupported BMP form: {depth} bits, compression {compression}")


def png_chunk(tag, body):
    """One PNG chunk: length, type, body, CRC."""
    return (struct.pack(">I", len(body)) + tag + body +
            struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))


def write_png(path, width, height, rows):
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter type None
        for pixel in row:
            raw.extend(pixel)
    body = png_chunk(b"IHDR", header) + png_chunk(
        b"IDAT", zlib.compress(bytes(raw), 9)) + png_chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" + body)


def main(argv):
    if len(argv) != 3:
        fail("usage: bmp2png.py <input.bmp> <output.png>")
    with open(argv[1], "rb") as handle:
        data = handle.read()
    width, height, rows = read_bmp(data)
    write_png(argv[2], width, height, rows)
    print(f"bmp2png: {argv[1]} -> {argv[2]} ({width}x{height})")


if __name__ == "__main__":
    main(sys.argv)
