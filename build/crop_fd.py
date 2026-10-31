import pypdfium2 as pdfium
from PIL import Image
import sys

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\MIMI-FOC\build\FD6288T.pdf")
page = pdf[3]  # 第4页，0-based 3
scale = 4.0
img = page.render(scale=scale).to_pil().convert("RGB")
W, H = img.size
print("page", W, H)

# 典型应用图：电路图大约在页面中上部
regions = {
    "typ": (0.12, 0.13, 0.90, 0.62),       # 整图
    "typ_vs": (0.62, 0.13, 0.90, 0.55),    # 右侧半桥 VS 节点
}
name = sys.argv[1] if len(sys.argv) > 1 else "typ"
xl, yt, xr, yb = regions[name]
out = img.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
p = rf"D:\100W\MY_PRO\MIMI-FOC\build\fd_{name}.png"
out.save(p)
print("saved", p, out.size)
