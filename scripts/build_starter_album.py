"""Build Sakura's original starter album, authored patterns and vector cover art.
Deterministic, no downloaded samples. Requires numpy, scipy, Pillow. Run from repo root.
The 40-bar arrangement: introduction, A, B, interlude, return, coda.
"""
from pathlib import Path
import json, math, wave
import numpy as np
from scipy.signal import lfilter
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SR = 44100
SONGS = [
    ("spring_breeze", 130, 62, [0, 5, 7, 2], (117, 186, 189), "01", "Spring Breeze"),
    ("cherry_blossom", 155, 57, [0, 5, 3, 7], (227, 157, 182), "02", "Cherry Blossom"),
    ("digital_dream", 175, 54, [0, 3, 5, 7], (135, 163, 224), "03", "Digital Dream"),
    ("sakura_storm", 200, 57, [0, 7, 5, 3], (211, 128, 162), "04", "Sakura Storm"),
]

def tone(midi, duration, voice):
    t=np.arange(int(duration*SR),dtype=np.float32)/SR
    f=440*2**((midi-69)/12)
    if voice=="pad":
        wave=(np.sin(2*np.pi*f*t)+0.45*np.sin(2*np.pi*f*1.003*t)+0.22*np.sin(4*np.pi*f*t))/1.67
        env=np.minimum(t/0.18,1)*np.minimum((duration-t)/0.5,1)
    elif voice=="bass":
        wave=np.sin(2*np.pi*f*t)+0.23*np.sin(4*np.pi*f*t)+0.08*np.sin(6*np.pi*f*t)
        env=np.minimum(t/0.008,1)*np.exp(-t*3)*np.minimum((duration-t)/0.025,1)
    else:
        wave=np.sin(2*np.pi*f*t+0.38*np.sin(2*np.pi*f*2*t)*np.exp(-t*7))+0.15*np.sin(4*np.pi*f*t)*np.exp(-t*6)
        env=np.minimum(t/0.004,1)*np.exp(-t*(5 if voice=="pluck" else 2.5))*np.minimum((duration-t)/0.035,1)
    return (wave*env).astype(np.float32)

def build_audio(folder,bpm,root,progression,seed):
    beat=60/bpm; duration=160*beat+2.5
    output=np.zeros((int(duration*SR),2),np.float32)
    rng=np.random.default_rng(seed); cache={}
    def add(at,samples,level=1,pan=0):
        start=int(at*SR); n=min(len(samples),len(output)-start)
        if n<=0:return
        output[start:start+n,0]+=samples[:n]*level*math.sqrt((1-pan)/2)
        output[start:start+n,1]+=samples[:n]*level*math.sqrt((1+pan)/2)
    def note(at,midi,length,voice,level,pan=0):
        key=(midi,round(length,5),voice)
        if key not in cache:cache[key]=tone(*key)
        add(at,cache[key],level,pan)
    t=np.arange(int(SR*0.38),dtype=np.float32)/SR
    kick=np.sin(2*np.pi*(49*t+8*(1-np.exp(-t*36))))*np.exp(-t*13)+rng.normal(0,0.09,len(t))*np.exp(-t*240)
    t=np.arange(int(SR*0.19),dtype=np.float32)/SR
    noise=rng.normal(0,1,len(t)).astype(np.float32)
    snare=(noise-lfilter([0.1],[1,-0.9],noise))*np.exp(-t*29)*0.30+np.sin(2*np.pi*190*t)*np.exp(-t*34)*0.20
    t=np.arange(int(SR*0.065),dtype=np.float32)/SR
    noise=rng.normal(0,1,len(t)).astype(np.float32)
    hat=(noise-lfilter([0.18],[1,-0.82],noise))*np.exp(-t*75)*0.15
    motif=[0,7,12,10,7,3,5,7,12,14,12,7,5,3,7,2]
    for bar in range(40):
        at=bar*4*beat; base=root+progression[(bar//2)%4]
        section=0 if bar<4 else 1 if bar<12 else 2 if bar<20 else 3 if bar<24 else 4 if bar<36 else 5
        bright=section in (2,4)
        for k,interval in enumerate([0,3 if (bar//2)%4!=1 else 4,7,14]):
            note(at,base+interval,beat*4.8,"pad",0.058,(-0.55+k*0.36))
        for step in range(8):
            pitch=base+12+[0,7,12,15,14,7,10,7][(step+bar%2*2)%8]
            note(at+step*beat/2,pitch,beat*1.8,"pluck",0.067 if section!=3 else 0.04,math.sin(step*1.1)*0.6)
        if section not in (0,3,5):
            for step in range(8 if bright else 4):
                pos=step*(0.5 if bright else 1)
                pitch=root+24+motif[(bar*3+step)%16]
                note(at+pos*beat,pitch,beat*1.5,"bell",0.12 if bright else 0.085,math.sin(bar*0.4)*0.2)
        if section!=3:
            for step in range(4):
                if section==0 and bar<2 and step%2:continue
                note(at+step*beat,base-24,beat*0.9,"bass",0.19)
                if step%2==0 or bright:add(at+step*beat,kick,0.48)
                if step%2 and section not in (0,5):add(at+step*beat,snare,0.55)
            for step in range(8 if bright else 4):
                add(at+step*beat*(0.5 if bright else 1),hat,0.32 if step%2 else 0.48,(-1 if step%2 else 1)*0.35)
            if bright and bar%4==3:
                for step in range(4):add(at+(3+step/4)*beat,snare,0.10+step*0.035,step*0.15-0.2)
    # A quiet stereo echo and short diffuse reflections retain clear percussion transients.
    dry=output.copy()
    for delay,gain in [(beat*0.75,0.12),(beat*1.5,0.065),(0.043,0.035),(0.073,0.025)]:
        n=int(delay*SR);output[n:]+=dry[:-n,::-1]*gain
    output*=np.minimum(np.arange(len(output))/SR/0.06,1)[:,None]
    output*=np.minimum((len(output)-np.arange(len(output)))/SR/2.3,1)[:,None]
    output=np.tanh(output*1.22)
    output*=0.89/max(0.89,float(np.max(np.abs(output))))
    with wave.open(str(folder/"music.wav"),"wb") as file:
        file.setnchannels(2);file.setsampwidth(2);file.setframerate(SR);file.writeframes((output*32767).astype("<i2").tobytes())
    return duration

def build_chart(bpm,difficulty):
    beat=60000/bpm;kb=[];mouse=[];ends=[-1]*4
    def add(b,lane,hold=0):
        time=round(b*beat)
        if time<=ends[lane]+70:return
        dur=round(hold*beat);kb.append(dict(time=time,lane=lane,type="hold" if hold else "tap",duration=dur));ends[lane]=time+dur
    for bar in range(2,40):
        quiet=bar<4 or 20<=bar<24 or bar>=36
        steps=2 if quiet else 4 if difficulty==0 else 8 if difficulty==1 else 12
        rhythm=[0,1,2,3] if steps==4 else [i*4/steps for i in range(steps)]
        if steps==12:rhythm=[0,0.5,0.75,1,1.5,1.75,2,2.5,2.75,3,3.5,3.75]
        if steps==2:rhythm=[0,2]
        for i,pos in enumerate(rhythm):
            lane=[0,1,3,2,1,0,2,3][(bar*2+i)%8]
            hold=(1.5 if quiet else 1) if i==0 and bar%4==0 else 0
            add(bar*4+pos,lane,hold)
            if difficulty==2 and i%6==0 and not quiet:add(bar*4+pos,(lane+2)%4)
        if bar>=38:continue
        count=1 if quiet or difficulty==0 else 2 if difficulty==1 else 3
        for i in range(count):
            b=bar*4+i*4/count+0.5
            x=0.5+math.sin(bar*1.4+i*1.8)*0.29;y=0.5+math.cos(bar*0.9+i*1.6)*0.26
            note=dict(time=round(b*beat),x=round(x,4),y=round(y,4),type="circle",slider_duration=0,slider_path=[])
            if i==0 and bar%4==1:
                length=2 if count==1 else 1
                note.update(type="slider",slider_duration=round(length*beat),slider_path=[[round(0.5+(x-0.5)*0.5,4),round(0.5-(y-0.5)*0.7,4)],[round(1-x,4),round(y,4)]])
            mouse.append(note)
    kb.sort(key=lambda n:(n["time"],n["lane"]));mouse.sort(key=lambda n:n["time"])
    sv=[dict(time=round(bar*4*beat),speed=speed,easing="linear") for bar,speed in [(0,1),(4,1.1),(20,0.85),(24,1.15),(32,1),(36,0.9)]] if difficulty==2 else [dict(time=0,speed=1,easing="linear")]
    return dict(version=2,timing_points=[dict(time=0,bpm=bpm,time_signature=[4,4])],sv_points=sv,keyboard_notes=kb,mouse_notes=mouse)

def cover(folder,color,index,title):
    n=1200;y,x=np.mgrid[0:n,0:n];k=(x+y)/(2*n)
    low=np.array([15,23,39]);high=np.array(color)*0.36
    pixels=low[None,None,:]*(1-k[:,:,None])+high[None,None,:]*k[:,:,None]
    rng=np.random.default_rng(int(index));pixels+=rng.normal(0,0.65,(n,n,1))
    img=Image.fromarray(np.uint8(np.clip(pixels,0,255))).convert("RGB");d=ImageDraw.Draw(img)
    cx,cy,r=n*.57,n*.43,n*.235
    for radius in [r+38,r+60]:d.ellipse([cx-radius,cy-radius,cx+radius,cy+radius],outline=tuple(int(c*.46) for c in color),width=2)
    d.ellipse([cx-r,cy-r,cx+r,cy+r],fill=tuple(min(255,int(c*.65+88)) for c in color))
    for layer in range(3):
        coords=[(xx,n*(.69+layer*.058)+math.sin(xx/n*10+layer*2)*n*.025+math.sin(xx/n*22)*n*.012) for xx in range(0,n+1,4)]
        d.polygon(coords+[(n,n),(0,n)],fill=(25-layer*5,34-layer*5,51-layer*6))
    def branch(x,y,angle,length,depth):
        ex=x+math.cos(angle)*length;ey=y+math.sin(angle)*length
        d.line([(x,y),(ex,ey)],fill=(79,60,77),width=2+depth*3)
        if depth:
            branch(ex,ey,angle-.36,length*.73,depth-1);branch(ex,ey,angle+.58,length*.61,depth-1)
        if depth<3:
            for i in range(3):
                fx=ex+math.sin(angle*8+i*4)*25;fy=ey+math.cos(angle*4+i*3)*20;size=8+i*2
                for j in range(5):
                    a=j*math.tau/5;px=fx+math.cos(a)*size*.65;py=fy+math.sin(a)*size*.65
                    d.ellipse([px-size*.65,py-size*.65,px+size*.65,py+size*.65],fill=tuple(min(255,int(c*.6+92+i*7)) for c in color))
                d.ellipse([fx-2,fy-2,fx+2,fy+2],fill=(250,226,188))
    branch(n*1.06,n*.04,2.75,n*.31,4)
    font=ROOT/"resources/fonts/NotoSansSC-Regular.ttf"
    d.text((n*.08,n*.08),"SAKURA   /   ORIGINALS",font=ImageFont.truetype(str(font),24),fill=color)
    d.text((n*.08,n*.73),index,font=ImageFont.truetype(str(font),96),fill=color)
    d.text((n*.08,n*.86),title,font=ImageFont.truetype(str(font),47),fill=(238,229,236))
    img.resize((600,600),Image.Resampling.LANCZOS).save(folder/"cover.png")

def build_training_tracks():
    for identifier,title,color in [("tutorial_song","First Steps",(156,200,182)),("test-song","Timing Garden",(174,161,211))]:
        folder=ROOT/"resources/charts"/identifier
        info=json.loads((folder/"info.json").read_text(encoding="utf-8"))
        chart=json.loads((folder/info["difficulties"][0]["chart_file"]).read_text(encoding="utf-8"))
        notes=sorted(chart.get("keyboard_notes",[])+chart.get("mouse_notes",[]),key=lambda n:n["time"])
        duration=max(n["time"]+max(n.get("duration",0),n.get("slider_duration",0)) for n in notes)/1000+2
        audio=np.zeros((int(duration*SR),2),dtype=np.float64)
        for i,note in enumerate(notes):
            sound=tone(62+[0,2,3,7,5,3,2,0][i%8],0.60,"bell")*0.21
            start=round(note["time"]/1000*SR);end=min(len(audio),start+len(sound));pan=(i%4-1.5)*0.10
            audio[start:end,0]+=sound[:end-start]*(0.7-pan);audio[start:end,1]+=sound[:end-start]*(0.7+pan)
        beat=60/info["bpm"]
        for i in range(int(duration/(beat*4))):
            start=round(i*4*beat*SR)
            for interval in [0,3,7]:
                sound=tone(50+interval,beat*4,"pad")*0.045;end=min(len(audio),start+len(sound))
                audio[start:end,:]+=sound[:end-start,None]
        fade=min(len(audio),SR);audio[-fade:]*=np.linspace(1,0,fade)[:,None]
        with wave.open(str(folder/"music.wav"),"wb") as wav:
            wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(SR);wav.writeframes((np.tanh(audio)*32700).astype("<i2").tobytes())
        info.update(title=title,artist="Sakura Soundworks",charter="Sakura",source="Sakura Practice Collection",background_file="",tags=["original","practice","easy"])
        (folder/"info.json").write_text(json.dumps(info,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        cover(folder,color,"00" if identifier=="tutorial_song" else "05",title)

def main():
    build_training_tracks()
    report=[]
    for identifier,bpm,root,progression,color,index,title in SONGS:
        folder=ROOT/"resources/charts"/identifier;info=json.loads((folder/"info.json").read_text(encoding="utf-8"))
        duration=build_audio(folder,bpm,root,progression,int(index))
        cover(folder,color,index,title)
        info.update(artist="Sakura Soundworks",charter="Sakura",source="Sakura Originals · Vol. 01",background_file="",preview_time=round(32*60000/bpm),tags=["original","mixed","electronic"])
        for d in info["difficulties"]:
            tier={"Easy":0,"Normal":0,"Hard":1,"Expert":2}.get(d["name"],1)
            chart=build_chart(bpm,tier)
            (folder/d["chart_file"]).write_text(json.dumps(chart,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
            d.update(note_count=len(chart["keyboard_notes"]),hold_count=sum(n["type"]=="hold" for n in chart["keyboard_notes"]),mouse_note_count=len(chart["mouse_notes"]))
        (folder/"info.json").write_text(json.dumps(info,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        report.append(dict(id=identifier,seconds=round(duration,2),sample_rate=SR,channels=2,difficulties=info["difficulties"]))
        print(identifier,round(duration,1),"seconds",flush=True)
    (ROOT/"resources/charts/album.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
if __name__=="__main__":main()
