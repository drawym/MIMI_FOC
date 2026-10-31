import pypdfium2 as pdfium
from PIL import Image
import sys

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\SCH_底板_2026-10-18.pdf")
page = pdf[0]
scale = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
img = page.render(scale=scale, fill_color=(255,255,255,255)).to_pil().convert("RGB")
W, H = img.size

# 相对坐标区域（左上为0）
regions = {
    "bridge": (0.135, 0.185, 0.395, 0.450),
    "driver": (0.170, 0.015, 0.405, 0.180),
    "u_bs": (0.285, 0.095, 0.360, 0.180),
    "u_ph": (0.138, 0.195, 0.225, 0.430),
    "v_ph": (0.225, 0.195, 0.310, 0.430),
    "w_ph": (0.310, 0.195, 0.390, 0.430),
}
name = sys.argv[1] if len(sys.argv) > 1 else "bridge"
xl, yt, xr, yb = regions[name]
out = img.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
p = rf"D:\100W\MY_PRO\MIMI-FOC\build\sch_{name}.png"
out.save(p)
print("saved", p, out.size)
