"""Dependency-free image input and output for the background design tools.

Nothing here imports an image library. The PNG halves write and read the
format with `zlib` and `struct` alone, and the BMP half is the reader of
`tests/harness/check_harness.py` (task T041) reduced to what the game's
checkpoint writer emits: 8-bit BI_RLE8, 8-bit BI_RGB, and the 24-bit
uncompressed escape, all with bottom-up rows.

One image in memory is a `Canvas`: `w * h` RGB triplets in one bytearray,
row-major from the top-left pixel, so a design paints it with slice
assignment instead of per-pixel calls.
"""

import struct
import zlib

# ---------------------------------------------------------------------------
# Canvas
# ---------------------------------------------------------------------------


class Canvas:
    """One RGB image in one flat bytearray, three bytes per pixel."""

    def __init__(self, width, height, fill):
        self.w = width
        self.h = height
        self.buf = bytearray(bytes(fill) * (width * height))

    def rect(self, x, y, w, h, color):
        """One filled axis-aligned rectangle, clipped to the canvas."""
        x0 = max(0, x)
        y0 = max(0, y)
        x1 = min(self.w, x + w)
        y1 = min(self.h, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        run = bytes(color) * (x1 - x0)
        for row in range(y0, y1):
            start = (row * self.w + x0) * 3
            self.buf[start:start + (x1 - x0) * 3] = run

    def pixel(self, x, y):
        base = (y * self.w + x) * 3
        return (self.buf[base], self.buf[base + 1], self.buf[base + 2])

    def key(self):
        """The whole image as one bytes object, for equality checks."""
        return bytes(self.buf)


def parse_hex(color):
    """'#RRGGBB' or 'RRGGBB' as an (r, g, b) tuple."""
    text = color.lstrip("#")
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


def hex_of(color):
    return "#%02X%02X%02X" % color


def luminance(color):
    """The WCAG relative luminance, the one brightness measure these tools
    use for every cap comparison."""
    def channel(value):
        part = value / 255.0
        return part / 12.92 if part <= 0.04045 else ((part + 0.055) / 1.055) ** 2.4

    return (0.2126 * channel(color[0]) + 0.7152 * channel(color[1]) +
            0.0722 * channel(color[2]))


def gray_of(color):
    """One 0..255 gray of `color` by luminance, the grayscale play mode."""
    return round(luminance(color) * 255)


# ---------------------------------------------------------------------------
# PNG output
# ---------------------------------------------------------------------------


def _png_chunk(tag, body):
    return (struct.pack(">I", len(body)) + tag + body +
            struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))


def write_png(path, canvas):
    """One 8-bit truecolor PNG, filter type 0 on every row, zlib level 9."""
    raw = bytearray()
    stride = canvas.w * 3
    for y in range(canvas.h):
        raw.append(0)
        raw += canvas.buf[y * stride:(y + 1) * stride]
    body = b"".join([
        struct.pack(">IIBBBBB", canvas.w, canvas.h, 8, 2, 0, 0, 0),
    ])
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(_png_chunk(b"IHDR", body))
        handle.write(_png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(_png_chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# PNG input
# ---------------------------------------------------------------------------


def _png_unfilter(rows, stride, width, height, bpp):
    """The reversed filters of a non-interlaced PNG scanline stream."""
    out = bytearray(width * height * bpp)
    previous = bytearray(width * bpp)
    for y in range(height):
        filter_type = rows[y * (stride + 1)]
        line = bytearray(rows[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filter_type == 0:
            pass
        elif filter_type == 1:  # Sub
            for i in range(bpp, len(line)):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(len(line)):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(len(line)):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(len(line)):
                left = line[i - bpp] if i >= bpp else 0
                up_left = previous[i - bpp] if i >= bpp else 0
                a = left
                b = up_left
                c = previous[i]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        else:
            raise ValueError("PNG filter type %d" % filter_type)
        out[y * width * bpp:(y + 1) * width * bpp] = line
        previous = line
    return out


def read_png(path):
    """One PNG as a Canvas. Reads every 8-bit non-interlaced form these
    tools write plus the palette form, with `zlib` and `struct` alone."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos = 8
    width = height = depth = color_type = None
    idat = bytearray()
    palette = []
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color_type = struct.unpack(">IIBB", body[:10])
        elif tag == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    if depth != 8 or color_type not in (0, 2, 3):
        raise ValueError("%s is %d-bit color type %d" % (path, depth, color_type))
    channels = {0: 1, 2: 3, 3: 1}[color_type]
    stride = width * channels
    rows = zlib.decompress(bytes(idat))
    pixels = _png_unfilter(rows, stride, width, height, channels)
    canvas = Canvas(width, height, (0, 0, 0))
    for y in range(height):
        for x in range(width):
            base = (y * width + x) * channels
            if color_type == 2:
                color = (pixels[base], pixels[base + 1], pixels[base + 2])
            elif color_type == 0:
                value = pixels[base]
                color = (value, value, value)
            else:
                color = palette[pixels[base]]
            target = (y * canvas.w + x) * 3
            canvas.buf[target:target + 3] = bytes(color)
    return canvas


# ---------------------------------------------------------------------------
# BMP input, after the harness reader
# ---------------------------------------------------------------------------


def _decode_rle8(data, offset, width, height):
    """The pixel indexes of one BI_RLE8 stream, top row first."""
    indexes = bytearray(width * height)
    pos = offset
    x = 0
    row = height - 1  # the format stores rows bottom-up
    while True:
        if pos + 2 > len(data):
            raise ValueError("truncated RLE8 stream")
        count, op = data[pos], data[pos + 1]
        pos += 2
        if count != 0:
            if x + count > width or row < 0:
                raise ValueError("RLE8 run past the row or canvas edge")
            start = row * width + x
            indexes[start:start + count] = bytes([op]) * count
            x += count
            continue
        if op == 0:  # end of line
            row -= 1
            x = 0
        elif op == 1:  # end of bitmap
            return indexes
        elif op == 2:  # delta
            if pos + 2 > len(data):
                raise ValueError("truncated RLE8 delta")
            x += data[pos]
            row -= data[pos + 1]
            pos += 2
            if x > width or row < 0:
                raise ValueError("RLE8 delta off the canvas")
        else:  # absolute run of literal indexes
            if pos + op > len(data):
                raise ValueError("truncated absolute RLE8 run")
            if x + op > width or row < 0:
                raise ValueError("absolute RLE8 run past the row edge")
            start = row * width + x
            indexes[start:start + op] = data[pos:pos + op]
            pos += op + (op % 2)
            x += op


def read_bmp(path):
    """One checkpoint BMP of the game's writer as a Canvas."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("%s is not a BMP" % path)
    offset = struct.unpack("<I", data[10:14])[0]
    width, raw_height = struct.unpack("<ii", data[18:26])
    depth = struct.unpack("<H", data[28:30])[0]
    compression = struct.unpack("<I", data[30:34])[0]
    height = abs(raw_height)
    canvas = Canvas(width, height, (0, 0, 0))

    def put(x, y, color):
        base = (y * width + x) * 3
        canvas.buf[base:base + 3] = bytes(color)

    if depth == 8:
        colors_used = struct.unpack("<I", data[46:50])[0] or 256
        palette = []
        for index in range(colors_used):
            entry = 54 + index * 4
            palette.append((data[entry + 2], data[entry + 1], data[entry]))
        if compression == 1:
            indexes = _decode_rle8(data, offset, width, height)
        elif compression == 0:
            stride = (width + 3) // 4 * 4
            if len(data) < offset + stride * height:
                raise ValueError("truncated 8-bit BMP")
            indexes = bytearray(width * height)
            for y in range(height):
                source = offset + y * stride
                target = (y if raw_height < 0 else height - 1 - y) * width
                indexes[target:target + width] = data[source:source + width]
        else:
            raise ValueError("8-bit compression %d" % compression)
        for y in range(height):
            for x in range(width):
                put(x, y, palette[indexes[y * width + x]])
        return canvas

    if compression != 0 or depth not in (24, 32):
        raise ValueError("%s has %d bits, compression %d" % (path, depth, compression))
    bpp = depth // 8
    stride = (width * bpp + 3) // 4 * 4
    if len(data) < offset + stride * height:
        raise ValueError("truncated BMP")
    for y in range(height):
        row = y if raw_height < 0 else height - 1 - y
        base = offset + row * stride
        for x in range(width):
            put(x, y, (data[base + 2], data[base + 1], data[base]))
    return canvas
