import os, re, sys
from PIL import Image, ImageDraw

# Repo root (this file lives in ios/tools/)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
src=open(os.path.join(ROOT, 'firmware/src/render/sprites.h')).read()
S={n:[r for r in re.findall(r'"([.#]{9})"',rows)] for n,rows in re.findall(r'//\s*(\w[\w ]*)\n((?:\s*"[.#]{9}",?\n?){9})',src)}
BG=(28,27,25); PEACH=(240,201,150); GREEN=(46,168,84); DIM=(120,100,78)
def glyph(d,name,cx,cy,px,col):
    rows=S[name]; x0=cx-9*px//2; y0=cy-9*px//2
    for y,r in enumerate(rows):
        for x,c in enumerate(r):
            if c=='#': d.rectangle([x0+x*px,y0+y*px,x0+(x+1)*px-1,y0+(y+1)*px-1],fill=col)
def make(layout,path):
    im=Image.new('RGB',(1024,1024),BG); d=ImageDraw.Draw(im)
    for name,cx,cy,px,col in layout: glyph(d,name,cx,cy,px,col)
    im.save(path)
out=os.path.join(ROOT, 'ios/build/icon/')
os.makedirs(out, exist_ok=True)
# A: five glyphs in a diamond, one green
p=26; g=250
make([('disc',512,512,p,PEACH),('plus',512,512-g,p,PEACH),('lozenge',512+g,512,p,GREEN),('ring',512,512+g,p,PEACH),('hash',512-g,512,p,PEACH)],out+'a.png')
# B: one big glyph + a spark
make([('lozenge',512,512,64,PEACH),('spark',780,250,26,GREEN)],out+'b.png')
# C: three glyphs, diagonal
make([('plus',300,300,30,PEACH),('disc',512,512,30,GREEN),('ring',724,724,30,PEACH)],out+'c.png')
# D: 2x2
p=30; g=150
make([('disc',512-g,512-g,p,PEACH),('plus',512+g,512-g,p,PEACH),('lozenge',512-g,512+g,p,PEACH),('block',512+g,512+g,p,GREEN)],out+'d.png')
ims=[Image.open(out+f) .resize((300,300),Image.NEAREST) for f in ['a.png','b.png','c.png','d.png']]
sheet=Image.new('RGB',(1260,330),(255,255,255))
for i,im in enumerate(ims):
    m=Image.new('L',(300,300),0); ImageDraw.Draw(m).rounded_rectangle([0,0,299,299],radius=67,fill=255)
    sheet.paste(im,(15+i*315,15),m)
sheet.save(out+'sheet.png')
