import pypdfium2 as pdfium
import zlib, struct, sys

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\SCH_底板_2026-10-18.pdf")
page = pdf[0]

def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw += row
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 7)) + chunk(b"IEND", b""))

scale = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
bmp = page.render(scale=scale, fill_color=(255,255,255,255))
W, H = bmp.width, bmp.height
st, nch = bmp.stride, bmp.n_channels
data = bytes(bmp.raw)

regions = {
    "full": (0.0, 0.0, 1.0, 1.0),
    "bridge": (0.120, 0.180, 0.430, 0.460),
    "u_bridge": (0.125, 0.190, 0.260, 0.400),
}
name = sys.argv[1] if len(sys.argv) > 1 else "full"
xl, yt, xr, yb = regions[name]
x0, y0, x1, y1 = int(xl*W), int(yt*H), int(xr*W), int(yb*H)
cw, chh = x1-x0, y1-y0
rows = []
for y in range(y0, y1):
    seg = data[y*st+x0*nch : y*st+x0*nch+cw*nch]
    seg = seg[:cw*nch]
    rgb = bytearray()
    for i in range(0, len(seg)-2, nch):
        B, G, R = seg[i], seg[i+1], seg[i+2]
        # 反色（黑底->白底）并转 RGB
        rgb += bytes((255-R, 255-G, 255-B))
    rows.append(bytes(rgb))
out = rf"D:\100W\MY_PRO\MIMI-FOC\build\sch_{name}.png"
write_png(out, cw, chh, rows)
print("saved", out, cw, chh)
