#!/usr/bin/env python3
"""mksprites.py OUT_SPRITE_FILE [OUT_PREVIEW_PNG]: makes the sprite file of Natter (the icon of the application and of the icon bar): a speech bubble, in two forms:
   !natter     the bubble
   natterhl    the bubble with a red mark (for the icon bar when something mentions you)
   natterbig   the bubble twice as big (for the information window)
32 bits a pixel with a mask, 68 x 68 pixels at 180 dots an inch (so it is drawn 34 pixels wide in a mode of 90 dots an inch).  Drawn at 4 times the size and reduced: the mask is on or off,
the colour at the edge is the colour of the bubble.  Only the standard library is needed (the preview PNG is written by hand)."""
import struct, sys, zlib

N = 68          # pixels of the small sprites (the drawing is 68 units wide; a bigger sprite scales it)
SS = 4          # drawing scale
DPI = 180

def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def inside_triangle(x, y, a, b, c):
    def sign(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])
    p = (x, y)
    d1, d2, d3 = sign(p, a, b), sign(p, b, c), sign(p, c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)

def sample(x, y, hl):
    """the colour (r, g, b) at a point of the 68 x 68 drawing (y down), or None outside the icon"""
    body = inside_round_rect(x, y, 6, 8, 62, 46, 9) or inside_triangle(x, y, (16, 44), (30, 44), (14, 60))
    if not body:
        return None
    edge = not (inside_round_rect(x, y, 8.5, 10.5, 59.5, 43.5, 6.5) or inside_triangle(x, y, (18, 43), (28, 43), (16.5, 55)))
    if edge:
        return (20, 70, 140)                                  # the outline
    for cx in (22, 34, 46):                                   # three dots: somebody is typing
        if (x - cx) ** 2 + (y - 27) ** 2 <= 9:
            return (20, 70, 140)
    if hl and (x - 55) ** 2 + (y - 13) ** 2 <= 49:           # the red mark
        return (215, 30, 30)
    if hl and (x - 55) ** 2 + (y - 13) ** 2 <= 81:
        return (255, 255, 255)
    return (235, 245, 255)                                    # the inside

def render(hl, n=N):
    sc = n / 68.0
    pix = [[None] * n for _ in range(n)]
    for py in range(n):
        for px in range(n):
            acc = []
            for sy in range(SS):
                for sx in range(SS):
                    c = sample((px + (sx + 0.5) / SS) / sc, (py + (sy + 0.5) / SS) / sc, hl)
                    if c: acc.append(c)
            if len(acc) * 2 >= SS * SS:                       # the mask: more than half covered
                pix[py][px] = tuple(sum(c[i] for c in acc) // len(acc) for i in range(3))
    # the red mark is drawn over the outline too
    return pix

def sprite_block(name, pix, n=N):
    N = n
    mode = 1 | (DPI << 1) | (DPI << 14) | (6 << 27)           # new format, 32 bits a pixel
    image = bytearray()
    for py in range(N):                                       # a sprite is stored from the top row down
        for px in range(N):
            c = pix[py][px] or (0, 0, 0)
            image += struct.pack("<I", (c[2] << 16) | (c[1] << 8) | c[0])
    words = (N + 31) // 32
    mask = bytearray()
    for py in range(N):
        row = [0] * words
        for px in range(N):
            if pix[py][px]:
                row[px // 32] |= 1 << (px % 32)
        mask += struct.pack("<%dI" % words, *row)
    header_size = 44
    total = header_size + len(image) + len(mask)
    h = struct.pack("<I12sIIIIIII", total, name.encode().ljust(12, b"\0"), N - 1, N - 1, 0, 31, header_size, header_size + len(image), mode)
    # the header is: next, name(12), width-1 (words), height-1 (rows), first bit, last bit, image, mask, mode
    h = struct.pack("<I", total) + name.encode().ljust(12, b"\0") + struct.pack("<IIIIIII", N - 1, N - 1, 0, 31, header_size, header_size + len(image), mode)
    assert len(h) == header_size
    return h + bytes(image) + bytes(mask)

def png(path, pix):
    raw = b""
    for row in pix:
        raw += b"\0" + b"".join(bytes(c) + b"\xff" if c else b"\0\0\0\0" for c in row)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

def main():
    a, b = render(False), render(True)
    big = render(False, 136)
    blocks = sprite_block("!natter", a) + sprite_block("natterhl", b) + sprite_block("natterbig", big, 136)
    area = struct.pack("<III", 3, 16, 16 + len(blocks)) + blocks
    open(sys.argv[1], "wb").write(area)
    if len(sys.argv) > 2:
        wide = [ra + rb for ra, rb in zip(a, b)]
        global N
        n0 = N
        N = n0 * 2
        png(sys.argv[2], wide) if False else None
        # a preview with both sprites side by side
        raw = b""
        for row in wide:
            raw += b"\0" + b"".join(bytes(c) + b"\xff" if c else b"\xf0\xf0\xf0\xff" for c in row)
        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n0 * 2, n0, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

main()
