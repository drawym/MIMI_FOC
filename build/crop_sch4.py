import pypdfium2 as pdfium
from PIL import Image
import sys

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\SCH_底板_2026-10-18.pdf")
page = pdf[0]
scale = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
bmp = page.render(scale=scale, fill_color=(255,255,255,255))
img = bmp.to_pil().convert("RGB")
W, H = img.size
print("full", W, H)

# 反色（EDA PDF 黑底）
inv = Image.eval(img, lambda v: 255 - v)

# 找非白内容边界框（反色后内容=非白，背景=白）
bbox = inv.getbbox()
print("content bbox", bbox, "rel", tuple(round(v/(W if i%2==0 else H),3) for i,v in enumerate(bbox)))

regions = {
    "full": (0.0, 0.0, 1.0, 1.0),
    "bridge": (0.120, 0.180, 0.430, 0.460),
}
name = sys.argv[1] if len(sys.argv) > 1 else "full"
if name == "full":
    out = inv
else:
    xl, yt, xr, yb = regions[name]
    out = inv.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
p = rf"D:\100W\MY_PRO\MIMI-FOC\build\sch_{name}.png"
out.save(p)
print("saved", p, out.size)
