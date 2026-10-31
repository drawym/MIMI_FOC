import pypdfium2 as pdfium
from PIL import Image

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_0402.pdf")
page = pdf[11]
img = page.render(scale=6.0).to_pil().convert("RGB")
W, H = img.size
# DC bias 曲线 0~20V 区间（整页比例）
xl, yt, xr, yb = 0.700, 0.300, 0.845, 0.600
out = img.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
out.save(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias_zoom.png")
print("saved", out.size)
