"""Native production-decoder regression. Writes synthetic fixtures only to --run-dir."""
from pathlib import Path
import argparse,json,subprocess,sys,random

def main():
    p=argparse.ArgumentParser();p.add_argument('--run-dir',type=Path,required=True)
    p.add_argument('--pillow-dir',type=Path,required=True);args=p.parse_args()
    run=args.run_dir.resolve();sys.path.insert(0,str(args.pillow_dir.resolve()))
    from PIL import Image
    folder=run/'jpeg-fixtures';folder.mkdir(exist_ok=True)
    exe=run/'photo_jpeg_runtime.exe';results=[]
    def call(name,path,w,h,cancel=0,fail=-1,expected=1):
        out=folder/'output.rgb'
        r=subprocess.run([str(exe),str(path),str(out),str(w),str(h),str(cancel),str(fail)],capture_output=True,text=True,timeout=10)
        assert r.returncode==0,(name,r.returncode,r.stderr)
        report=json.loads(r.stdout);report['name']=name
        if expected is not None:assert report['result']==expected,(name,report)
        results.append(report);return report,out
    samples=[]
    for color,sub,w,h in [('RGB',2,257,193),('RGB',0,320,241),('RGB',1,319,157),('L',0,193,161),('RGB',2,1,1)]:
        image=Image.new(color,(w,h))
        image.putdata([((x*3+y)%256,(y*2+x)%256,(x+y*3)%256) if color=='RGB' else (x+y)%256 for y in range(h) for x in range(w)])
        path=folder/f'{color}-{sub}-{w}.jpg';image.save(path,quality=90,subsampling=sub,progressive=True)
        samples.append(path)
        for ow,oh in [(w,h),(min(120,w),min(90,h))]:
            report,out=call(f'progressive-{color}-{sub}-{w}-{ow}',path,ow,oh)
            assert report['progressive']==1 and (report['width'],report['height'])==(w,h)
            denom=next(d for d in (8,4,2,1) if (w+d-1)//d>=ow and (h+d-1)//d>=oh)
            reference=Image.open(path)
            reference.draft('RGB',(max(1,w//denom),max(1,h//denom)))
            reference=reference.convert('RGB');rw,rh=reference.size
            pix=reference.load();expected=bytes(v for y in range(oh) for x in range(ow) for v in pix[(2*x+1)*rw//(2*ow),(2*y+1)*rh//(2*oh)])
            actual=out.read_bytes();errors=[abs(a-b) for a,b in zip(actual,expected)]
            assert len(actual)==ow*oh*3 and max(errors,default=0)<=3,(path,denom,max(errors))
            report['max_rgb_error']=max(errors,default=0)
    base=folder/'baseline.jpg';Image.new('RGB',(64,64),(30,170,90)).save(base)
    report,_=call('baseline',base,64,64);assert report['progressive']==0
    call('header-only',samples[0],0,0)
    big=folder/'oversize.jpg';Image.new('RGB',(1024,768)).save(big,progressive=True)
    call('original-coefficient-limit',big,240,180,expected=-2)
    cmyk=folder/'cmyk.jpg';Image.new('CMYK',(64,64)).save(cmyk,progressive=True)
    call('unsupported-colorspace',cmyk,64,64,expected=0)
    twelve=run/'upstream-jpeg/libjpeg-turbo-3.1.2/testimages/testorig12.jpg'
    call('unsupported-12bit',twelve,32,32,expected=0)
    original=samples[0].read_bytes()
    for cut in (1,2,8,len(original)//2):
        path=folder/'truncated.jpg';path.write_bytes(original[:-cut]);call(f'truncated-{cut}',path,120,90,expected=0)
    for at in (1,2,8,40):call(f'cancel-{at}',samples[0],120,90,cancel=at,expected=-3)
    for index in range(45):
        report,_=call(f'allocation-failure-{index}',samples[0],120,90,fail=index,expected=None)
        assert report['result'] in (-1,1),report
    # Corrupt source/header data can succeed or fail, but must not crash, leak,
    # escape the output canary or run indefinitely.
    rng=random.Random(20260922)
    for index in range(50):
        data=bytearray(original)
        for _ in range(1+index%5):data[rng.randrange(len(data))]^=rng.randrange(1,256)
        path=folder/'mutated.jpg';path.write_bytes(data)
        call(f'mutation-{index}',path,120,90,expected=None)
    (run/'jpeg-runtime.json').write_text(json.dumps({'passed':True,'checks':len(results),'results':results},indent=2))
    print(f'JPEG native cases: {len(results)} passed (pixel reference, caps, cancellation, allocation failure, corrupt input)')

if __name__=='__main__':main()
