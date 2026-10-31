from PIL import Image

img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias.png").convert("L")
px = img.load()
W, H = img.size

# 小图坐标系：0V竖线 x≈430，50V竖线 x≈1158；0%横线 y≈150，-20% y≈235（先扫描校准）
# 找横轴 0V 和 50V 竖线：在曲线下方（y=300~650）扫描深色竖线
def vlines():
    res=[]
    for x in range(420, 1170):
        c=sum(1 for y in range(280,660) if px[x,y]<150)
        if c>150: res.append((x,c))
    return res
vs=vlines()
# 合并相邻
def cluster(lst):
    out=[]
    for x,c in lst:
        if out and x-out[-1][-1]<=2: out[-1].append(x)
        else: out.append([x])
    return [(sum(g)//len(g)) for g in out]
vx=cluster(vs)
print("vertical grid x:", vx)

# 找横线：在 x=450~1150 扫描
def hlines():
    res=[]
    for y in range(95,670):
        c=sum(1 for x in range(450,1150) if px[x,y]<150)
        if c>300: res.append((y,c))
    return res
hs=cluster(hlines())
print("horizontal grid y:", hs)

# 读曲线在给定电压下的y
def curve_y(volt):
    x0=vx[0]; x1=vx[-1]
    x=int(x0+(volt/50.0)*(x1-x0))
    # 在 x 附近 +-6 搜索最上方的深色点（曲线），跳过网格（y 必须在网格线之间）
    found=[]
    for xx in range(x-6,x+7):
        for y in range(100,660):
            if px[xx,y]<110:
                found.append((xx,y))
    return x, found
for v in [10,12,20]:
    x,ys=curve_y(v)
    ys_sorted=sorted(set(y for _,y in ys))
    print(f"V={v} x={x} dark y values sample:", ys_sorted[:15])
