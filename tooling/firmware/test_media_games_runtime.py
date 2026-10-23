"""Exercise production PCM, synth, maze and raw USB I/O with synthetic data only."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,math,os,subprocess,sys
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=True)
    dll=run/'probe.dll'
    commands=[('compile',[str(a.compiler.resolve()),'-std=c++17','-O2','-fno-exceptions','-fno-rtti','-shared','-static-libgcc','-static-libstdc++','-I'+str(ROOT/'main'),str(ROOT/'../tooling/firmware/native/media_games_probe.cpp'),'-o',str(dll)])]
    for name,cmd in commands:
        env=dict(os.environ);env['PATH']=str(a.compiler.resolve().parent)+os.pathsep+env.get('PATH','')
        result=subprocess.run(cmd,cwd=ROOT,capture_output=True,timeout=90,env=env)
        (run/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
    native=C.CDLL(str(dll));outcomes={}
    for name in ('usb_probe','synth_probe','maze_probe'):
        fn=getattr(native,name);fn.restype=C.c_int;rc=fn();assert rc==0,(name,'failure at production probe line',rc);outcomes[name]=True
    speed=native.speed_probe;speed.argtypes=[C.POINTER(C.c_int16),C.c_uint,C.c_uint,C.POINTER(C.c_int16)];speed.restype=C.c_uint
    def convert(samples,chunk=1024):
        source=(C.c_int16*len(samples))(*samples);dest=(C.c_int16*(len(samples)//2+2))()
        n=speed(source,len(samples),chunk,dest);assert n==len(samples)//2
        return list(dest[:n])
    signal=[round(12000*math.sin(2*math.pi*500*i/16000)) for i in range(8193)]
    reference=convert(signal)
    for chunk in (1,2,7,37,255,512,1023):assert convert(signal,chunk)==reference,chunk
    crossings=sum(reference[i]<=0<reference[i+1] for i in range(100,len(reference)-1))
    measured=crossings*16000/(len(reference)-101);assert 990<measured<1010,measured
    def rms(values):return math.sqrt(sum(x*x for x in values)/len(values))
    stopband=convert([round(12000*math.sin(2*math.pi*7000*i/16000)) for i in range(8192)])
    attenuation=20*math.log10(rms(stopband[100:])/rms(reference[100:]));assert attenuation<-15,attenuation
    assert max(abs(x) for x in convert([32767,-32768]*4096))<=32768
    outcomes['rewind_resampler']={'measured_hz_for_500hz_input':round(measured,1),'alias_attenuation_db':round(attenuation,1),'chunk_boundaries_match':True}
    render=native.render_maze;render.argtypes=[C.c_uint,C.POINTER(C.c_uint16),C.POINTER(C.c_uint32),C.POINTER(C.c_int)]
    for level in range(15):
        floor=(C.c_uint16*(240*240))();ball=(C.c_uint32*144)();details=(C.c_int*4)();render(level,floor,ball,details)
        rgb=bytearray()
        for n in floor:rgb.extend(((n>>11)*255//31,((n>>5)&63)*255//63,(n&31)*255//31))
        # Composite the real ball pixels and simple goal marker for inspection.
        sx,sy,gx,gy=details
        for y in range(gy-9,gy+10):
            for x in range(gx-9,gx+10):
                d=(x-gx)**2+(y-gy)**2
                if 36<=d<=81:rgb[(y*240+x)*3:(y*240+x)*3+3]=bytes((243,180,92))
        for y in range(12):
            for x in range(12):
                n=ball[y*12+x]
                if n>>24:rgb[((sy+y-6)*240+sx+x-6)*3:((sy+y-6)*240+sx+x-6)*3+3]=bytes(((n>>16)&255,(n>>8)&255,n&255))
        (run/f'maze-{level+1:02d}.ppm').write_bytes(b'P6\n240 240\n255\n'+rgb)
    outcomes['passed']=True;outcomes['synthetic_only']=True
    outcomes['inputs']={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ('main/core/usb_storage_io.hpp','main/core/recorder_transport.hpp','main/core/game_synth.hpp','main/apps/maze_model.hpp','main/apps/maze_visual.hpp','../tooling/firmware/native/media_games_probe.cpp')}
    (run/'result.json').write_text(json.dumps(outcomes,indent=2));print(json.dumps(outcomes,indent=2))
if __name__=='__main__':main()
