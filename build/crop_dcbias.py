import pypdfium2 as pdfium
from PIL import Image

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_0402.pdf")
page = pdf[11]  # 第12页
img = page.render(scale=4.0).to_pil().convert("RGB")
W, H = img.size
print("page", W, H)
# DC bias 图在右侧中间
xl, yt, xr, yb = 0.60, 0.26, 0.95, 0.56
out = img.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
out.save(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias.png")
print("saved", out.size)
