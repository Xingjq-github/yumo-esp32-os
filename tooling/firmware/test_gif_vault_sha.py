"""Native production decoder/SHA tests; synthetic fixtures, no device/SD data."""
from pathlib import Path
import argparse, ctypes as C, hashlib, io, json, os, random, statistics, subprocess, time
from PIL import Image, ImageDraw
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',required=True)
    ap.add_argument('--gif-only',action='store_true')
    ap.add_argument('--baseline-library',type=Path);a=ap.parse_args()
    run=Path(a.run_dir).resolve();run.mkdir(parents=True,exist_ok=True)
    cc=Path(__import__('shutil').which('gcc') or 'gcc')
    env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    src=ROOT/'components/photo_gif';lib=run/'media.dll'
    args=[str(cc),'-shared','-O2','-Wall','-Wextra','-I'+str(src),'-I'+str(src/'giflib'),
          str(src/'photo_gif.c'),*[str(src/'giflib'/s) for s in ('dgif_lib.c','gifalloc.c','gif_err.c','openbsd-reallocarray.c')],
          *([] if a.gif_only else [str(ROOT/'main/core/vault_sha256.c')]),'-o',str(lib)]
    p=subprocess.run(args,env=env,capture_output=True,text=True);(run/'compile.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
    dll=C.CDLL(str(lib));count=0
    class Sha(C.Structure):
        _fields_=[('length',C.c_uint64),('state',C.c_uint32*8),('curlen',C.c_uint32),('buf',C.c_uint8*64)]
    if not a.gif_only:
        dll.vault_sha256_init.argtypes=[C.POINTER(Sha)]
        dll.vault_sha256_process.argtypes=[C.POINTER(Sha),C.c_void_p,C.c_ulong]
        dll.vault_sha256_done.argtypes=[C.POINTER(Sha),C.c_void_p]
    rng=random.Random(711)
    for n in ([] if a.gif_only else [0,1,3,55,56,63,64,65,127,128,1023,1024,1025,18257,262144,1000000]):
        data=rng.randbytes(n)
        for chunk in [1,63,64,1024,max(n,1)]:
            state=Sha();out=(C.c_uint8*32)();dll.vault_sha256_init(C.byref(state))
            for i in range(0,n,chunk):
                piece=data[i:i+chunk];assert dll.vault_sha256_process(C.byref(state),piece,len(piece))==0
            assert dll.vault_sha256_done(C.byref(state),out)==0
            assert bytes(out)==hashlib.sha256(data).digest(),(n,chunk)
            count+=1
    sha_count=count
    READ=C.CFUNCTYPE(C.c_int,C.c_void_p,C.POINTER(C.c_uint8),C.c_int)
    SEEK=C.CFUNCTYPE(C.c_int,C.c_void_p);CANCEL=C.CFUNCTYPE(C.c_int,C.c_void_p)
    def signatures(d):
        d.photo_gif_open.argtypes=[C.c_void_p,READ,SEEK,CANCEL];d.photo_gif_open.restype=C.c_void_p
        for name in ['photo_gif_close','photo_gif_width','photo_gif_height','photo_gif_restart']:
            getattr(d,name).argtypes=[C.c_void_p]
        d.photo_gif_next.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_uint)]
    signatures(dll)
    def decode(data,expected=None,cycles=1,cancel_at=0):
        nonlocal count
        pos=0;calls=0
        @READ
        def read(_,out,n):
            nonlocal pos
            chunk=data[pos:pos+n];C.memmove(out,chunk,len(chunk));pos+=len(chunk);return len(chunk)
        @SEEK
        def seek(_):
            nonlocal pos
            pos=0;return 0
        @CANCEL
        def cancel(_):
            nonlocal calls
            calls+=1;return int(cancel_at>0 and calls>=cancel_at)
        g=dll.photo_gif_open(None,read,seek,cancel)
        if not g:
            assert expected is None;count+=1;return
        try:
            w=dll.photo_gif_width(g);h=dll.photo_gif_height(g);out=(C.c_uint16*(w*h))();delay=C.c_uint()
            assert 0<w<=240 and 0<h<=240
            for cycle in range(cycles):
                frames=0
                while True:
                    rc=dll.photo_gif_next(g,out,w*h,C.byref(delay));count+=1
                    if rc!=1:break
                    assert frames<200
                    if expected is not None:
                        frame=expected[frames].convert('RGBA');sw,sh=frame.size
                        for y in range(h):
                            for x in range(w):
                                r,gg,b,alpha=frame.getpixel((x*sw//w,y*sh//h))
                                v=((r>>3)<<11)|((gg>>2)<<5)|(b>>3) if alpha else 0
                                assert out[y*w+x]==v,(cycle,frames,x,y,out[y*w+x],v)
                        duration=expected[frames].info.get('duration',100)
                        assert delay.value==(duration if duration>10 else 100),(delay.value,duration)
                    frames+=1
                if expected is not None:assert rc==0 and frames==len(expected)
                if cycle+1<cycles:assert dll.photo_gif_restart(g)==1
        finally:dll.photo_gif_close(g)
    fixtures=[]
    palette=[0,0,0,255,0,0,0,255,0,0,0,255,255,255,255]+[0,0,0]*251
    for size in [(31,23),(17,41),(241,151)]:
        for dispose in [1,2,3]:
            frames=[]
            for i in range(4):
                im=Image.new('P',size,0);im.putpalette(palette)
                ImageDraw.Draw(im).rectangle((2+i*2,3+i,12+i*2,15+i),fill=1+i)
                frames.append(im)
            b=io.BytesIO();frames[0].save(b,format='GIF',save_all=True,append_images=frames[1:],
                duration=[50,80,100,200],loop=0,disposal=dispose,transparency=0,optimize=True)
            data=b.getvalue();fixtures.append(data);image=Image.open(io.BytesIO(data));expected=[]
            for i in range(image.n_frames):image.seek(i);expected.append(image.convert('RGBA').copy())
            decode(data,expected,2)
    # Single image with interlacing and opaque global palette.
    im=Image.new('RGB',(81,91));pixels=im.load()
    for y in range(91):
        for x in range(81):pixels[x,y]=(255 if x%3 else 0,255 if y%3 else 0,255 if (x+y)%3 else 0)
    b=io.BytesIO();im.save(b,format='GIF',interlace=True);data=b.getvalue();fixtures.append(data)
    decode(data,[Image.open(io.BytesIO(data)).convert('RGBA')],2)
    # Legal fast/uneven delays, sub-centisecond fallback, and large downscales.
    for size in [(240,240),(480,320),(1024,768),(1,7),(7,1)]:
        frames=[]
        for i in range(7):
            im=Image.new('P',size);im.putpalette(palette)
            im.putdata([(x//5+y//3+i)%5 for y in range(size[1]) for x in range(size[0])]);frames.append(im)
        b=io.BytesIO();frames[0].save(b,format='GIF',save_all=True,append_images=frames[1:],
            duration=[20,30,70,0,10,200,655350],loop=0,disposal=1,optimize=False)
        data=b.getvalue();fixtures.append(data);image=Image.open(io.BytesIO(data));expected=[]
        for i in range(image.n_frames):image.seek(i);expected.append(image.convert('RGBA').copy())
        decode(data,expected,2)
    for i,data in enumerate(fixtures):(run/f'fixture-{i}.gif').write_bytes(data)
    for data in fixtures:
        for end in range(0,len(data),max(1,len(data)//35)):decode(data[:end])
        for at in [1,2,5,20,50,100]:decode(data,cancel_at=at)
    for n in [0,1,6,40,2000]:decode(rng.randbytes(n))
    bad=bytearray(fixtures[0]);bad[6:10]=b'\xff\xff\xff\xff';decode(bytes(bad))
    benchmark={}
    if a.baseline_library:
        baseline=C.CDLL(str(a.baseline_library.resolve()));signatures(baseline)
        # Same in-memory I/O/cancellation callbacks, warm repeated decoder loops.
        # This is a host comparison, not a measurement of the SD card or LCD.
        def measure(d,data):
            pos=0
            @READ
            def read(_,out,n):
                nonlocal pos
                chunk=data[pos:pos+n];C.memmove(out,chunk,len(chunk));pos+=len(chunk);return len(chunk)
            @SEEK
            def seek(_):
                nonlocal pos
                pos=0;return 0
            @CANCEL
            def cancel(_):return 0
            g=d.photo_gif_open(None,read,seek,cancel);assert g
            w=d.photo_gif_width(g);h=d.photo_gif_height(g);out=(C.c_uint16*(w*h))();delay=C.c_uint();samples=[]
            try:
                for batch in range(6):
                    start=time.perf_counter_ns();n=0
                    for loop in range(12):
                        while (rc:=d.photo_gif_next(g,out,w*h,C.byref(delay)))==1:n+=1
                        assert rc==0;assert d.photo_gif_restart(g)==1
                    if batch:samples.append((time.perf_counter_ns()-start)/n/1000000)
            finally:d.photo_gif_close(g)
            return round(statistics.median(samples),4)
        for i in [0,10,11]:
            benchmark[str(i)]={'before_ms_per_frame':measure(baseline,fixtures[i]),'after_ms_per_frame':measure(dll,fixtures[i])}
    result={'passed':True,'sha_cases':sha_count,'decoder_operations':count-sha_count,'fixtures':len(fixtures),
            'oracle':('Pillow' if a.gif_only else 'hashlib and Pillow')+', exact RGB565 output; disposal 1/2/3, interlace, scaling, looping, truncation, cancellation',
            'real_data_used':False,'host_decode_benchmark':benchmark,'exact_frame_delays':True}
    (run/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':main()
