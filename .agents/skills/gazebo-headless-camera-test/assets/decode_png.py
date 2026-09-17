import re, sys, zlib, struct


def unescape(txt):
    """Decode gz-topic octal-escaped binary (e.g. \\313 -> byte 0xD3)."""
    out = bytearray()
    i = 0
    while i < len(txt):
        c = txt[i]
        if c == "\\" and i + 1 < len(txt) and txt[i + 1] in "01234567":
            j = i + 1
            o = ""
            while j < len(txt) and len(o) < 3 and txt[j] in "01234567":
                o += txt[j]
                j += 1
            out.append(int(o, 8))
            i = j
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


def write_png(path, w, h, rgb):
    """Write an 8-bit RGB PNG (no external deps)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0
        raw.extend(rgb[y * w * 3:(y + 1) * w * 3])

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


raw = open(sys.argv[1], "rb").read().decode("latin-1")
out_png = sys.argv[2]
m = re.search(r"width: (\d+)\nheight: (\d+)\nstep: (\d+)", raw)
w, h, step = int(m.group(1)), int(m.group(2)), int(m.group(3))
dm = re.search(r"data: \"(.*?)\"\n\n", raw, re.S) or re.search(r"data: \"(.*?)\"", raw, re.S)
data = unescape(dm.group(1))
print(f"frame {w}x{h} step={step} decoded_len={len(data)} expected={w * h * 3}")

px = w * h
minv, maxv = 255, 0
red = 0
rows = {}
for y in range(h):
    for x in range(w):
        i = (y * w + x) * 3
        r, g, b = data[i], data[i + 1], data[i + 2]
        lum = (r + g + b) // 3
        minv = min(minv, lum)
        maxv = max(maxv, lum)
        if r > 120 and g < 90 and b < 90:
            red += 1
            rows[y] = rows.get(y, 0) + 1
print(f"luminance min={minv} max={maxv} (dynamic range => real scene)")
print(f"red px={red} ({100 * red / px:.1f}%)")
nr = [y for y, v in rows.items() if v > 0]
if nr:
    print(f"red box rows {min(nr)}..{max(nr)} of {h} (center {h // 2}) => target in view")
write_png(out_png, w, h, data)
print(f"wrote PNG -> {out_png}")
