import pypdfium2 as pdfium
import zlib, struct, sys

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\SCH_底板_2026-10-18.pdf")
page = pdf[0]
Wpt, Hpt = page.get_size()
print("page pt", Wpt, Hpt)

def write_png(path, width, height, bgra, n_channel):
    # 转 RGB
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter byte
        for x in range(width):
            i = (y*width + x)*n_channel
            b = bgra[i]; g = bgra[i+1]; r = bgra[i+2]
            raw += bytes((r, g, b))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        c += struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
        return c
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8bit RGB
    idat = zlib.compress(bytes(raw), 6)
    with open(path, "wb") as f:
        f.write(sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b""))

# 区域名 -> (left,bottom,right,top) 相对坐标
regions = {
    "bridge": (0.11, 0.52, 0.43, 0.86),
    "driver": (0.13, 0.66, 0.27, 0.85),
}
name = sys.argv[1] if len(sys.argv) > 1 else "bridge"
l, b, r, t = regions[name]
clip = (l*Wpt, b*Hpt, r*Wpt, t*Hpt)
scale = 9.0
bmp = page.render(scale=scale, clip=clip)
img = bmp.to_pil() if False else None
w, h = bmp.get_size()
mode = bmp.get_geometry().format if hasattr(bmp, "get_geometry") else "BGRA"
data = bmp.get_bitmap_data()
nch = 4 if len(data) >= w*h*4 else 3
out = rf"D:\100W\MY_PRO\MIMI-FOC\build\sch_{name}.png"
write_png(out, w, h, data, nch)
print("saved", out, w, h, "ch", nch, "mode", mode)
