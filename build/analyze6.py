from PIL import Image
img = Image.open(r"D:\100W\MY_PRO\MIMI-FOC\build\dc_full.png").convert("L")
px=img.load(); W,H=img.size
def cluster(vals):
    out=[]
    for v in vals:
        if out and v-out[-1][-1]<=5: out[-1].append(v)
        else: out.append([v])
    return [sum(g)//len(g) for g in out]
# 网格区目测：x 230~1230, y 400~890
vx=[]
for x in range(220,1240):
    if sum(1 for y in range(420,880) if px[x,y]<190)>250: vx.append(x)
hy=[]
for y in range(390,890):
    if sum(1 for x in range(230,1230) if px[x,y]<190)>500: hy.append(y)
vx=cluster(vx); hy=cluster(hy)
print("vx",vx)
print("hy",hy)
# 假设 vx 对应 0,10,20,30,40,50；hy 对应 20,0,-20,-40,-60,-80,-100
def x_of(v):
    return vx[0]+(v/50.0)*(vx[-1]-vx[0])
# 0% 是 hy 中第2条
y0=[y for y in hy if 470<y<510]
ym20=[y for y in hy if 555<y<590]
y0=y0[0]; ym20=ym20[0]
print("y0",y0,"y-20",ym20)
def pct(y): return -(y-y0)/((ym20-y0)/20.0)
def curve(v):
    xc=int(x_of(v)); ys=[]
    for xx in range(xc-10,xc+11):
        for y in range(380,700):
            if px[xx,y]<100: ys.append(y)
    if not ys: return None
    ys.sort(); return ys[len(ys)//2],len(ys)
for v in [0,5,10,12,15,20]:
    r=curve(v)
    if r: print(f"V={v:2d}  change={pct(r[0]):5.1f}%  (effective {100+pct(r[0]):.0f}%, {100*(1+pct(r[0])/100):.0f}nF)  npix={r[1]}")
