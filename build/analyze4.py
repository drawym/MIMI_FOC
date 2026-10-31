from PIL import Image

img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\murata_dcbias_zoom.png").convert("L")
px = img.load()
W, H = img.size

def cluster(vals):
    out=[]
    for v in vals:
        if out and v-out[-1][-1]<=4: out[-1].append(v)
        else: out.append([v])
    return [sum(g)//len(g) for g in out]

# 竖网格线（灰色），宽阈值
vx=[]
for x in range(250,730):
    c=sum(1 for y in range(450,950) if px[x,y]<190)
    if c>300: vx.append(x)
print("vx:", cluster(vx))

# 横网格线
hy=[]
for y in range(380,960):
    c=sum(1 for x in range(290,720) if px[x,y]<190)
    if c>300: hy.append(y)
print("hy:", cluster(hy))
