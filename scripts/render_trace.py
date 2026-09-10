"""Render an actual portable-model trace. This is not Unreal footage or an EQS capture."""
from __future__ import annotations
import argparse
import csv
import itertools
import json
import pathlib
from PIL import Image, ImageDraw, ImageFont

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--trace",type=pathlib.Path,default=ROOT/"evidence/portable/trace.csv")
    parser.add_argument("--episode",type=pathlib.Path,default=ROOT/"evidence/portable/trace-episode.json")
    parser.add_argument("--output",type=pathlib.Path,default=ROOT/"docs/media/portable-trace.gif")
    args=parser.parse_args()
    rows=list(csv.DictReader(args.trace.open(encoding="utf-8")))
    episode=json.loads(args.episode.read_text(encoding="utf-8"))
    if episode.get("engine")!="portable-cpp-model":
        raise ValueError("Renderer only accepts portable-model episode metadata")
    font_path=pathlib.Path("C:/Windows/Fonts/consola.ttf")
    font=ImageFont.truetype(str(font_path),15) if font_path.exists() else ImageFont.load_default(size=15)
    small=ImageFont.truetype(str(font_path),12) if font_path.exists() else ImageFont.load_default(size=12)
    title=ImageFont.truetype(str(font_path),21) if font_path.exists() else ImageFont.load_default(size=21)
    frames=[]
    def pos(x,y):return (int(55+(float(x)+20)*16),int(110+(15-float(y))*16))
    for time,group in itertools.groupby(rows,key=lambda row:row["time"]):
        actors=list(group)
        image=Image.new("RGB",(1024,650),(10,16,24));draw=ImageDraw.Draw(image)
        draw.text((30,20),"AEGIS ARENA / POLICY LAB",font=title,fill=(225,233,242))
        draw.text((30,52),"PORTABLE C++ MODEL TRACE - NOT UNREAL ENGINE FOOTAGE",font=font,fill=(255,195,90))
        draw.text((30,77),f"seed {episode['seed']}  |  companion {episode['policy']}  |  t = {float(time):05.2f} s",font=small,fill=(150,171,190))
        draw.rectangle((55,110,695,590),outline=(61,88,106),width=2)
        for x in range(-20,21,5):
            a=pos(x,-15);b=pos(x,15);draw.line((*a,*b),fill=(23,37,48))
        for y in range(-15,16,5):
            a=pos(-20,y);b=pos(20,y);draw.line((*a,*b),fill=(23,37,48))
        for x,y,r in ([(-3,4,1.8),(3,-4,1.8),(6,6,1.4),(-7,-6,1.4)] if episode['arena']=='pillars' else []):
            px,py=pos(x,y);radius=int(r*16);draw.ellipse((px-radius,py-radius,px+radius,py+radius),fill=(46,62,75),outline=(78,99,114),width=2)
        by_id={int(a["id"]):a for a in actors}
        for i,a in enumerate(actors):
            x,y=pos(a["x"],a["y"]);hp=float(a["health"]);identifier=int(a["id"])
            color=(72,190,247) if identifier==0 else ((103,222,161) if identifier==1 else (243,110,100))
            if hp<=0:
                draw.line((x-5,y-5,x+5,y+5),fill=(90,95,105),width=2);draw.line((x-5,y+5,x+5,y-5),fill=(90,95,105),width=2)
            else:
                target=by_id.get(int(a["target"]))
                if a["visible"]=="1" and target:
                    tx,ty=pos(target["x"],target["y"]);draw.line((x,y,tx,ty),fill=tuple(int(c*0.35) for c in color))
                draw.ellipse((x-8,y-8,x+8,y+8),fill=color)
                draw.text((x+10,y-8),str(identifier),font=small,fill=color)
                draw.rectangle((x-10,y+13,x+10,y+16),fill=(34,43,53))
                maxhp=130 if identifier==0 else (100 if identifier==1 else (140 if identifier==episode['enemies']+1 and episode['enemies']>=4 else 65))
                draw.rectangle((x-10,y+13,x-10+int(20*hp/maxhp),y+16),fill=color)
            label="PLAYER" if identifier==0 else ("COMPANION" if identifier==1 else f"ENEMY {identifier-1}")
            draw.text((725,120+i*69),label,font=font,fill=color)
            draw.text((725,144+i*69),f"{a['state']}  HP {hp:.0f}",font=small,fill=(202,215,227))
            draw.text((725,162+i*69),f"observed target {a['target']}  LOS {a['visible']}",font=small,fill=(130,150,170))
        draw.text((55,615),"Actual compiled C++ episode  |  5 Hz decisions  |  CSV -> reproducible replay",font=small,fill=(153,174,192))
        frames.append(image)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    frames[0].save(args.output,save_all=True,append_images=frames[1:],duration=200,loop=0,optimize=True)
    frames[len(frames)//2].save(args.output.with_suffix(".png"))
    print(f"Rendered {len(frames)} trace frames to {args.output}")

if __name__=="__main__":main()
