from PIL import Image

img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias_zoom.png").convert("L")
px = img.load()

# 0V x=413, 10V x=612 → 每V=19.9px; 0% y=491, -20% y=572 → 每%=4.05px
def x_of(v): return 413 + v*19.9
def pct_of(y): return -(y-491)/4.05

def curve_y_at(v):
    xc=int(x_of(v))
    ys=[]
    for xx in range(xc-8, xc+9):
        for y in range(380,620):
            if px[xx,y] < 100:   # 仅黑色曲线（网格是浅灰）
                ys.append(y)
    if not ys: return None
    # 取中位数（曲线在网格交叉处，用中位更稳）
    ys.sort()
    return ys[len(ys)//2], ys

for v in [0,5,10,12,15,20]:
    r=curve_y_at(v)
    if r:
        ymed, ys=r
        print(f"V={v:2d}  x={int(x_of(v)):3d}  curve y={ymed:3d}  change={pct_of(ymed):5.1f}%  npix={len(ys)}")
