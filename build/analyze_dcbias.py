from PIL import Image

img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias.png").convert("L")
px = img.load()
W, H = img.size
print("size", W, H)

def dc(x, y): return 1 if px[x, y] < 120 else 0

# 找纵轴网格横线（贯穿 x 范围 430~1160）
print("--- horizontal grid lines (y: count) ---")
for y in range(80, 680, 2):
    c = sum(dc(x, y) for x in range(430, 1158, 3))
    if c > 180: print(y, c)
print("--- vertical grid lines (x: count) ---")
for x in range(420, 1170, 2):
    c = sum(dc(x, y) for y in range(110, 660, 3))
    if c > 120: print(x, c)
