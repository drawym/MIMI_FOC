from PIL import Image

img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias_zoom.png").convert("L")
px = img.load()
W, H = img.size
print("size", W, H)

# 找竖网格线（0/10/20V），扫描 y 区间 0%以下(500~900)
def cluster(vals):
    out=[]
    for v in vals:
        if out and v-out[-1][-1]<=3: out[-1].append(v)
        else: out.append([v])
    return [sum(g)//len(g) for g in out]

vx=[]
for x in range(0,W):
    c=sum(1 for y in range(480,920) if px[x,y]<140)
    if c>200: vx.append(x)
vx=cluster(vx)
print("vertical grid x:", vx)

hy=[]
for y in range(380,920):
    c=sum(1 for x in range(280,720) if px[x,y]<140)
    if c>250: hy.append(y)
hy=cluster(hy)
print("horizontal grid y:", hy)
