import pypdfium2 as pdfium
from PIL import Image

pdf = pdfium.PdfDocument(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_0402.pdf")
page = pdf[11]
img = page.render(scale=6.0).to_pil().convert("RGB")
W, H = img.size
print("page", W, H)
xl, yt, xr, yb = 0.695, 0.300, 0.965, 0.605
out = img.crop((int(xl*W), int(yt*H), int(xr*W), int(yb*H)))
out.save(r"D:\100W\MY_PRO\MIMI-FOC\build\dc_full.png")
print("saved", out.size)
