"""Real loader/codec regression under a bounded host PSRAM model; synthetic SD only."""
from pathlib import Path
import argparse,hashlib,json,os,random,re,struct,subprocess,sys,zlib

ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',type=Path,required=True);a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to((ROOT/'artifacts/validation').resolve())
    run.mkdir(parents=True,exist_ok=False)
    from PIL import Image
    gcc=Path(__import__('shutil').which('gcc') or 'gcc').resolve();gpp=gcc.with_name('g++.exe')
    env=dict(os.environ);env['PATH']=str(gcc.parent)+os.pathsep+env.get('PATH','')
    core=ROOT/'firmware/main/core';jpeg=ROOT/'firmware/components/photo_jpeg'
    png=ROOT/'dependencies/managed_components/espressif__libpng'
    z=ROOT/'dependencies/managed_components/espressif__zlib/zlib'
    (run/'probe.h').write_text('''#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *probe_malloc(size_t);void *probe_calloc(size_t,size_t);
void *probe_realloc(void *,size_t);void probe_free(void *);
void probe_config(size_t,size_t,long);
size_t probe_live(void);size_t probe_used(void);size_t probe_peak(void);size_t probe_attempts(void);
#ifdef __cplusplus
}
#endif
''')
    (run/'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#ifdef __cplusplus
extern "C" {
#endif
void *heap_caps_malloc(size_t,unsigned);void *heap_caps_calloc(size_t,size_t,unsigned);
void *heap_caps_realloc(void *,size_t,unsigned);void heap_caps_free(void *);
size_t heap_caps_get_free_size(unsigned);size_t heap_caps_get_largest_free_block(unsigned);
#ifdef __cplusplus
}
#endif
''')
    source=re.sub(r'^#include .*\n','',(core/'photo_loader.cpp').read_text(encoding='utf-8'),flags=re.M)
    for name in ('fopen','fclose','fread','fseek','fwrite','fflush'):source=source.replace('std::'+name,'fakeF'+name[1:])
    source=source.replace('fstat(fileno(', 'fakeFstat(fileno(')
    for name,replace in (('::open(', 'fakeOpen('),('::fdopen(', 'fakeFdopen('),('::close(', 'fakeCloseFd('),('std::remove(', 'fakeRemove(')):source=source.replace(name,replace)
    (run/'photo_loader.inc').write_text(source,encoding='utf-8')
    cache_source=re.sub(r'^#include .*\n','',(core/'photo_frame_cache.cpp').read_text(encoding='utf-8'),flags=re.M)
    for name in ('fopen','fclose','fread','fseek','fwrite','fflush'):cache_source=cache_source.replace('std::'+name,'fakeF'+name[1:])
    cache_source=cache_source.replace('fstat(fileno(', 'fakeFstat(fileno(')
    for name,replace in (('::open(', 'fakeOpen('),('::fdopen(', 'fakeFdopen('),('::close(', 'fakeCloseFd('),
        ('std::remove(', 'fakeRemove('),('std::rename(', 'fakeRename('),('::fsync(', 'fakeFsync('),
        ('::opendir(', 'fakeOpendir('),('::readdir(', 'fakeReaddir('),('::closedir(', 'fakeClosedir(')):
        cache_source=cache_source.replace(name,replace)
    (run/'photo_frame_cache.inc').write_text(cache_source,encoding='utf-8')
    cfiles=[core/'photo_png.c',jpeg/'photo_jpeg.c',jpeg/'jpeg_memory.c']
    cfiles += [jpeg/p for p in re.findall(r'"(upstream/[^\"]+\.c)"',(jpeg/'CMakeLists.txt').read_text())]
    cfiles += [p for p in (png/'libpng').glob('png*.c') if p.name!='pngtest.c']
    cfiles += [z/(n+'.c') for n in ('adler32','compress','crc32','deflate','infback','inffast','inflate','inftrees','trees','uncompr','zutil')]
    includes=[run,core,ROOT/'firmware/main',jpeg,jpeg/'include',jpeg/'upstream',png,png/'libpng',z,
              ROOT/'dependencies/managed_components/espressif__esp_new_jpeg/include']
    flags=['-O2','-DPNGCAPI=','-DPNG_USER_PRIVATEBUILD=1','-DESP_PLATFORM']+['-I'+str(p) for p in includes]
    def execute(name,compiler,args):
        rsp=run/(name+'.rsp');rsp.write_text(subprocess.list2cmdline([str(v).replace('\\','/') for v in args]),encoding='utf-8')
        p=subprocess.run([str(compiler),'@'+str(rsp)],cwd=run,env=env,capture_output=True,timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
        print(name+' passed',flush=True)
    execute('compile-codecs',gcc,flags+['-std=c11','-include',run/'probe.h','-Dmalloc=probe_malloc',
            '-Dcalloc=probe_calloc','-Dfree=probe_free','-Drealloc=probe_realloc','-c']+cfiles)
    heap=ROOT/'tooling/firmware/native/photo_stream_heap.c';replay=ROOT/'tooling/firmware/native/photo_stream_replay.cpp'
    execute('compile-heap',gcc,flags+['-std=c11','-c',heap])
    exe=run/'photo-stream.exe'
    execute('compile-loader',gpp,flags+['-std=c++17','-Wall','-Wextra','-static-libgcc','-static-libstdc++',replay,
            *sorted(run.glob('*.o')),'-o',exe])
    fixtures=run/'fixtures';fixtures.mkdir();results=[];checks=0
    def call(name,path,mode='normal',width=0,ok=True,pan=(0,0)):
        nonlocal checks
        original_hash=sha(path)
        out=fixtures/(name+'.rgb565')
        p=subprocess.run([str(exe),str(path),str(out),mode,str(width),str(pan[0]),str(pan[1])],cwd=run,env=env,capture_output=True,timeout=120,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        (run/(name+'.log')).write_bytes(p.stdout+p.stderr)
        assert p.returncode==0,(name,p.returncode,p.stderr.decode(errors='replace'))
        assert sha(path)==original_hash,(name,'original image changed')
        result=json.loads(p.stdout);result.update(name=name,file_bytes=path.stat().st_size)
        assert result['released'] and result['peak']<=2800*1024 and result['max_read']<=4096,result
        if result['ok']:assert result['width']<=240 and result['height']<=240,result
        if ok is not None:assert result['ok']==ok,result
        results.append(result);checks+=result['checks'];return result,out
    def pngchunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    def padded(path,target):
        raw=path.read_bytes();remaining=target-len(raw);extra=[]
        overhead=4 if path.suffix=='.jpg' else 12
        assert remaining>=overhead
        while remaining:
            count=min(16000,remaining)
            if 0<remaining-count<overhead:count-=overhead
            payload=b'A'*(count-overhead)
            extra.append(b'\xff\xfe'+struct.pack('>H',len(payload)+2)+payload if overhead==4 else pngchunk(b'vpAg',payload))
            remaining-=count
        p=fixtures/(path.stem+'-pad-'+str(target)+path.suffix)
        # Legal ignored metadata, before image data (never padding after EOI/IEND).
        pos=2 if overhead==4 else 33
        p.write_bytes(raw[:pos]+b''.join(extra)+raw[pos:]);assert p.stat().st_size==target;return p
    bayer=(0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5)
    def reference(path,result,out,jpeg_image=False):
        w,h=result['width'],result['height'];vw,vh=result['scaled_width'],result['scaled_height'];ox,oy=result['x'],result['y'];im=Image.open(path)
        if jpeg_image:
            iw,ih=im.size;denom=next(d for d in (8,4,2,1) if (iw+d-1)//d>=vw and (ih+d-1)//d>=vh)
            im.draft('RGB',(max(1,iw//denom),max(1,ih//denom)))
        im=im.convert('RGB');iw,ih=im.size;pix=im.load();raw=out.read_bytes();assert len(raw)==w*h*2
        worst=0
        for y in range(h):
            for x in range(w):
                rgb=pix[(2*(ox+x)+1)*iw//(2*vw),(2*(oy+y)+1)*ih//(2*vh)];threshold=bayer[(oy+y)%4*4+(ox+x)%4]
                expected=[v*levels//255+int(v*levels%255*32>(2*threshold+1)*255) for v,levels in zip(rgb,(31,63,31))]
                color=int.from_bytes(raw[(y*w+x)*2:(y*w+x)*2+2],'little');actual=(color>>11,(color>>5)&63,color&31)
                worst=max(worst,max(abs(a-b) for a,b in zip(expected,actual)))
        assert worst<=(1 if jpeg_image else 0),(path,worst)
        result['max_channel_quantization_error']=worst
    rng=random.Random(20261020)
    # Real high-entropy pixels account for most of the 16 MiB file, separately
    # from exact-boundary fixtures padded with legal metadata.
    large_png=fixtures/'large.png';Image.frombytes('RGB',(2400,2304),rng.randbytes(2400*2304*3)).save(large_png)
    assert 15*1024*1024<large_png.stat().st_size<16*1024*1024
    large_jpg=fixtures/'large.jpg';Image.frombytes('RGB',(3072,2048),rng.randbytes(3072*2048*3)).save(large_jpg,quality=95,subsampling=0)
    assert 12*1024*1024<large_jpg.stat().st_size<16*1024*1024
    for path in (large_png,large_jpg):
        r,out=call(path.stem+path.suffix[1:],path);assert not r['cached'];reference(path,r,out,path.suffix=='.jpg')
        exact=padded(path,16*1024*1024)
        r,frame=call(path.suffix[1:]+'-16MiB',exact);assert frame.read_bytes()==out.read_bytes()
        r,zoom=call(path.suffix[1:]+'-zoom',exact,width=360);assert r['opens']==2;reference(path,r,zoom,path.suffix=='.jpg')
        call(path.suffix[1:]+'-zoom-limit',exact,width=1600,pan=(177,119))
        above=padded(path,16*1024*1024+1);call(path.suffix[1:]+'-over-limit',above,ok=False)
    print('Real large images, exact 16 MiB and zoom limits passed',flush=True)
    # Memory and stream adapters must render identically across old formats.
    samples=[]
    for kind in ('RGB','RGBA','L','P','I;16'):
        im=Image.new(kind,(193,161))
        if kind=='I;16':im.putdata([(x*193+y*127)%65536 for y in range(161) for x in range(193)])
        elif kind=='P':
            im.putpalette([v for i in range(256) for v in (i,255-i,(i*7)%256)]);im.putdata([(x+y)%256 for y in range(161) for x in range(193)]);im.info['transparency']=0
        else:im.putdata([tuple((x*3+y*k)%256 for k in range(1,len(kind)+1)) if kind!='L' else (x+y)%256 for y in range(161) for x in range(193)])
        path=fixtures/(kind.replace(';','')+'.png');im.save(path);samples.append(path)
    # A genuine Adam7 file, with known filter-zero pass rows.
    w,h=65,33;pixels=bytes((x*3+y*k)%256 for y in range(h) for x in range(w) for k in (1,2,3));data=[]
    for sx,sy,dx,dy in ((0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)):
        for y in range(sy,h,dy):data.append(b'\0'+b''.join(pixels[(y*w+x)*3:(y*w+x)*3+3] for x in range(sx,w,dx)))
    adam=fixtures/'adam7.png';adam.write_bytes(b'\x89PNG\r\n\x1a\n'+pngchunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,1))+pngchunk(b'IDAT',zlib.compress(b''.join(data)))+pngchunk(b'IEND',b''));samples.append(adam)
    for progressive in (False,True):
        for sub in (0,1,2):
            p=fixtures/f'jpeg-{progressive}-{sub}.jpg';Image.open(samples[0]).convert('RGB').save(p,progressive=progressive,subsampling=sub,quality=90);samples.append(p)
    for size in ((1,1),(127,65)):
        p=fixtures/f'gray-{size[0]}.jpg';Image.new('L',size,129).save(p,progressive=True);samples.append(p)
    for i,path in enumerate(samples):
        r,small=call(f'format-{i}-cached',path);assert r['cached']
        stream=padded(path,300*1024);r,large=call(f'format-{i}-stream',stream);assert not r['cached']
        assert small.read_bytes()==large.read_bytes(),path
        if path.suffix=='.jpg' or path.name in ('RGB.png','adam7.png'):reference(path,r,large,path.suffix=='.jpg')
    small=samples[0];call('cached-no-sd-zoom',small,'cached-zoom',width=120)
    progressive=fixtures/'large-progressive.jpg';Image.new('RGB',(1024,768)).save(progressive,progressive=True)
    r,out=call('progressive-swap',progressive);assert r['swap_opens']>0;reference(progressive,r,out,True)
    stream=padded(fixtures/'jpeg-True-2.jpg',300*1024)
    for mode in ('open-fail','read-fail','close-fail','session-fail','stat-fail','seek-fail','cancel','sleep','eject','timeout','low-memory','fragmented','task-fail'):
        call(mode,stream,mode,ok=False)
    for mode in ('generation-change','size-change','time-change','content-change'):
        call(mode,stream,mode,width=150,ok=False)
    for i in range(55):call('allocation-'+str(i),stream,'allocation-'+str(i),ok=None)
    for path in (large_jpg,large_png):
        broken=fixtures/('truncated'+path.suffix);broken.write_bytes(path.read_bytes()[:-20]);call('truncated-'+path.suffix[1:],broken,ok=False)
    empty=fixtures/'empty.png';empty.write_bytes(b'');call('empty',empty,ok=False)
    for w,h in ((16385,1),(16384,8192)):
        invalid=fixtures/f'dimensions-{w}-{h}.png'
        invalid.write_bytes(b'\x89PNG\r\n\x1a\n'+pngchunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+pngchunk(b'IDAT',zlib.compress(b'\0'))+pngchunk(b'IEND',b''))
        call(f'dimensions-{w}-{h}',invalid,ok=False)
    cmyk=fixtures/'cmyk.jpg';Image.new('CMYK',(64,64)).save(cmyk);call('unsupported-cmyk',cmyk,ok=False)
    for mode in ('swap-busy','swap-directory','swap-full','swap-info','swap-create','swap-fdopen','swap-header','swap-flush','swap-write','swap-read','swap-seek','swap-close','swap-remove','swap-cancel','swap-sleep','swap-eject','swap-timeout','swap-foreign'):
        r,_=call(mode,progressive,mode,ok=False);assert r['error'] or r['cancelled'] or mode in ('swap-sleep',),r
    r,_=call('swap-stale',progressive,'swap-stale');assert r['swap_opens']
    for i in range(65):call('swap-allocation-'+str(i),progressive,'allocation-'+str(i),ok=None)
    # Real 24 MP originals, including progressive coefficient spill, now open.
    base=Image.new('RGB',(6000,4000),(42,130,216))
    for kind in ('png','jpg','progressive'):
        p=fixtures/('24mp-'+kind+('.png' if kind=='png' else '.jpg'))
        base.save(p,progressive=kind=='progressive',subsampling=2)
        r,out=call('24mp-'+kind,p);reference(p,r,out,kind!='png')
        if kind=='progressive':assert r['swap_opens']>0
        r,out=call('24mp-'+kind+'-corner',p,width=6000,pan=(5760,3760));reference(p,r,out,kind!='png')
        assert r['width']==r['height']==240 and r['x']==5760 and r['y']==3760
    too_large=fixtures/'progressive-cache-cap.jpg';base.save(too_large,progressive=True,subsampling=0)
    call('progressive-cache-cap',too_large,ok=False)
    # Random crop positions and repeated logical scales match independent pixels.
    for p in (large_png,large_jpg,adam,fixtures/'jpeg-True-2.jpg'):
        for i in range(4):
            iw,ih=Image.open(p).size;scale=max(min(iw,500+i*400),min(iw,240))
            r,out=call(p.stem+'-roi-'+str(i),p,width=scale,pan=(53+i*131,31+i*97));reference(p,r,out,p.suffix=='.jpg')
    for w,h in ((1,16000),(16000,1),(17,301),(301,17)):
        p=fixtures/f'slender-{w}-{h}.png';Image.new('RGB',(w,h),(80,140,200)).save(p)
        r,out=call(p.stem,p);reference(p,r,out)
    textured=fixtures/'textured-progressive.jpg'
    Image.frombytes('RGB',(1024,768),rng.randbytes(1024*768*3)).save(textured,progressive=True,quality=92)
    for p in (large_png,large_jpg,adam,progressive,textured,fixtures/'RGBA.png',fixtures/'P.png',fixtures/'I16.png'):
        width=min(Image.open(p).width,1600)
        r,out=call('tiles-'+p.stem,p,'tiles-roundtrip',width=width,pan=(177,119))
        assert r['cache_hit'];reference(p,r,out,p.suffix=='.jpg') if p.name not in ('RGBA.png','P.png','I16.png') else None
    for mode in ('tiles-corrupt','tiles-truncate'):
        r,out=call(mode,small,mode);assert r['cache_hit'];reference(small,r,out)
    for mode in ('tiles-directory','tiles-full','tiles-create','tiles-fdopen','tiles-write','tiles-read',
                 'tiles-seek','tiles-close','tiles-flush','tiles-sync','tiles-rename','tiles-foreign','tiles-memory','tiles-busy'):
        r,out=call(mode,small,mode);assert not r['cache_hit'];reference(small,r,out)
    for mode in ('tiles-cancel','tiles-sleep','tiles-eject'):call(mode,small,mode,ok=False)
    r,out=call('tiles-replace',padded(fixtures/'jpeg-False-2.jpg',300*1024),'tiles-replace');assert r['cache_hit']
    for mode in ('tiles-policy','tiles-stale','tiles-foreign-final'):
        r,out=call(mode,small,mode);reference(small,r,out)
    source_inputs=[core/'photo_loader.cpp',core/'photo_loader.hpp',core/'photo_frame_cache.cpp',core/'photo_frame_cache.hpp',
                   jpeg/'include/photo_sink.h',core/'photo_png.h',jpeg/'include/photo_jpeg.h',jpeg/'include/photo_view.h',
                   ROOT/'firmware/main/apps/photo_pixels.hpp',core/'sd_media.hpp',jpeg/'jpeg_port.h',heap,replay,Path(__file__)]+cfiles
    source_inputs += list((jpeg/'upstream').glob('*.h'))+list((png/'libpng').glob('*.h'))+[png/'pnglibconf.h']
    report=dict(passed=True,cases=len(results),assertions=checks,results=results,
                method='Actual C++ loader and production PNG/libjpeg codecs; bounded host allocator and synthetic SD/RTOS; Espressif fast decoder replaced by failing adapter to exercise safe fallback',
                psram_capacity=2800*1024,device_accessed=False,real_device_speed_measured=False,
                inputs={p.relative_to(ROOT).as_posix():sha(p) for p in source_inputs},
                fixtures={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in fixtures.iterdir() if p.suffix in ('.png','.jpg')})
    (run/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'{len(results)} image cases and {checks} lifecycle/memory assertions passed',flush=True)

if __name__=='__main__':main()
