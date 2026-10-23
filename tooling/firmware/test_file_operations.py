"""Exercise actual file operations exclusively on a new synthetic host tree."""
from pathlib import Path
import argparse,ctypes as C,errno,json,os,subprocess
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',required=True);a=ap.parse_args()
    run=Path(a.run_dir).resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    cc=Path(__import__('shutil').which('g++') or 'g++')
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    cmd=[str(cc),'-std=c++17','-O2','-Wall','-Wextra','-shared','-static-libgcc','-static-libstdc++','-I'+str(ROOT/'main'),
        str(ROOT/'../tooling/firmware/native/file_operations_replay.cpp'),str(ROOT/'main/core/file_operations.cpp'),'-o',str(run/'files.dll')]
    p=subprocess.run(cmd,env=env,capture_output=True);(run/'compile.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode()
    dll=C.CDLL(str(run/'files.dll'))
    class Result(C.Structure):
        _fields_=[(x,C.c_int) for x in ('error','more','count','changed','steps')]+[('bytes',C.c_ulonglong),('names',(C.c_char*256)*32)]
    dll.file_run.argtypes=[C.c_char_p,C.c_int,C.c_char_p,C.c_char_p,C.c_uint,C.c_int,C.POINTER(Result)]
    dll.file_page.argtypes=[C.c_char_p,C.c_char_p,C.c_char_p,C.c_int,C.c_int,C.POINTER(Result)]
    dll.file_name.argtypes=[C.c_char_p];dll.file_path.argtypes=[C.c_char_p,C.c_char_p,C.c_int]
    tree=run/'synthetic-card';tree.mkdir();root=tree.as_posix().encode();checks=[]
    def op(name,kind,source='',target='',offset=0,cancel=0,ok=True):
        s=(tree/source).as_posix().encode();t=(tree/target).as_posix().encode() if target else b''
        out=Result();dll.file_run(root,kind,s,t,offset,cancel,C.byref(out))
        assert (out.error==0)==ok,(name,out.error)
        checks.append(name);return out
    for name in ('a','abc.txt','folder name','hidden.name','中文目录','a-b_1'):
        assert dll.file_name(name.encode());checks.append('valid '+name)
    for name in ('','.','..','bad/child','bad\\child','name.','name ','x:y','x?y','x*y','\n','a'*256):
        assert not dll.file_name(name.encode());checks.append('invalid '+repr(name))
    for tail in ('../escape','a/../escape','a//b','a/','./a','a/./b'):
        assert not dll.file_path(root,root+b'/'+tail.encode(),0);checks.append('reject '+tail)
    outside=run/'outside';outside.write_text('untouched')
    for kind in range(1,6):op('reject root mutation '+str(kind),kind,ok=False)
    assert outside.read_text()=='untouched'
    op('new folder',1,'source');op('nested folder',1,'source/sub')
    data=bytes(range(256))*1000;(tree/'source/a.bin').write_bytes(data);(tree/'source/sub/b.txt').write_text('hello')
    class Properties(C.Structure):
        _fields_=[(n,C.c_int) for n in ('error','valid','directory','complete')]+[(n,C.c_uint) for n in ('files','folders')]+[('bytes',C.c_ulonglong),('modified',C.c_longlong),('changed',C.c_uint)]
    dll.file_properties.argtypes=[C.c_char_p,C.c_char_p,C.c_int,C.POINTER(Properties)]
    def properties(name,cancel=0):
        out=Properties();dll.file_properties(root,(tree/name).as_posix().encode(),cancel,C.byref(out))
        assert out.changed==0
        return out
    p=properties('source/a.bin');assert not p.error and p.valid and p.complete and not p.directory
    assert p.bytes==len(data) and p.modified==int((tree/'source/a.bin').stat().st_mtime)
    p=properties('source');assert not p.error and p.directory and p.complete and p.bytes==len(data)+5 and (p.files,p.folders)==(2,1)
    (tree/'empty').mkdir();p=properties('empty');assert p.complete and (p.bytes,p.files,p.folders)==(0,0,0)
    p=properties('source',cancel=3);assert p.error==errno.ECANCELED and not p.complete
    p=properties('missing');assert p.error==errno.ENOENT and not p.valid
    p=properties('../outside');assert p.error==errno.EINVAL and not p.valid
    deep=tree/'deep';deep.mkdir()
    for i in range(18):deep=deep/'x';deep.mkdir()
    p=properties('deep');assert p.error==dll.file_loop_error() and p.valid and not p.complete and p.directory
    assert (tree/'source/a.bin').read_bytes()==data and (tree/'source/sub/b.txt').read_text()=='hello'
    # Finished and cancelled reads leave no handles behind.
    for i in range(12):
        p=properties('source',cancel=3 if i%2 else 0)
        (tree/'source').rename(tree/'read-cycle');(tree/'read-cycle').rename(tree/'source')
    checks += ['file metadata/mtime', 'recursive folder totals', 'empty folder', 'property cancellation', 'missing/path rejection', 'bounded partial statistics', 'read-only and closed handles']
    op('new existing rejected',1,'source',ok=False)
    result=op('copy directory',3,'source','copy');assert result.bytes==len(data)+5
    assert (tree/'copy/a.bin').read_bytes()==data and (tree/'copy/sub/b.txt').read_text()=='hello'
    op('same-name copy rejected',3,'source','copy',ok=False)
    op('copy into self rejected',3,'source','source/sub/child',ok=False)
    op('case-insensitive self rejected',3,'source/a.bin','source/A.BIN',ok=False)
    op('move directory',4,'copy','moved');assert not (tree/'copy').exists()
    op('rename file',2,'moved/a.bin','moved/renamed.bin');assert (tree/'moved/renamed.bin').read_bytes()==data
    op('move onto existing rejected',4,'moved/renamed.bin','source/a.bin',ok=False)
    for n in (1,2,3,5,8,12,20,50):
        op('cancel copy '+str(n),3,'source','cancelled',cancel=n,ok=False)
        assert not (tree/'cancelled').exists() and (tree/'source/a.bin').read_bytes()==data
        assert not list(tree.glob('.yumo-copy-*'))
    op('cancel move preserves source',4,'moved','nothing',cancel=1,ok=False);assert (tree/'moved').is_dir()
    op('cancel deletion before writes',5,'moved',cancel=1,ok=False);assert (tree/'moved/renamed.bin').exists()
    op('delete directory tree',5,'moved');assert not (tree/'moved').exists()
    op('delete missing rejected',5,'moved',ok=False)
    op('rename invalid destination',2,'source/a.bin','../outside',ok=False);assert outside.read_text()=='untouched'
    op('missing parent rejected',1,'missing/child',ok=False)
    op('cannot create under file',1,'source/a.bin/child',ok=False)
    op('create list',1,'many')
    for i in range(101):(tree/'many'/f'{i:03}.txt').write_text(str(i))
    names=[]
    for i in range(4):
        result=op('paged list '+str(i),0,'many',offset=i*32)
        assert bool(result.more)==(i<3)
        names += [bytes(result.names[n]).split(b'\0')[0].decode() for n in range(result.count)]
    assert names==[f'{i:03}.txt' for i in range(101)]
    for i in reversed(range(40)):(tree/'many'/f'folder{i:02}').mkdir()
    expected=[f'folder{i:02}' for i in range(40)]+[f'{i:03}.txt' for i in range(101)]
    names=[];anchor=b'';directory=False;pages=[]
    while True:
        out=Result();dll.file_page(root,(tree/'many').as_posix().encode(),anchor,directory,False,C.byref(out));assert not out.error
        page=[bytes(out.names[n]).split(b'\0')[0].decode() for n in range(out.count)]
        pages.append(page);names+=page
        if not out.more:break
        anchor=page[-1].encode();directory=page[-1].startswith('folder')
    assert names==expected
    for i in range(len(pages)-1,0,-1):
        out=Result();anchor=pages[i][0];dll.file_page(root,(tree/'many').as_posix().encode(),anchor.encode(),anchor.startswith('folder'),True,C.byref(out))
        assert not out.error and [bytes(out.names[n]).split(b'\0')[0].decode() for n in range(out.count)]==pages[i-1]
    partial=op('nonaligned sorted page',0,'many',offset=5)
    assert [bytes(partial.names[n]).split(b'\0')[0].decode() for n in range(partial.count)]==expected[5:37]
    checks.append('global directory-first order and forward/back cursor pagination')
    # Repeated completed/cancelled cycles also catch leaked Windows FILE/DIR
    # handles: subsequent rename/rmdir would fail if a handle remained open.
    for i in range(30):
        op('cycle copy '+str(i),3,'source','cycle');op('cycle move '+str(i),4,'cycle','cycle2');op('cycle remove '+str(i),5,'cycle2')
    assert (tree/'source/a.bin').read_bytes()==data and not list(tree.glob('.yumo-copy-*'))
    result={'passed':True,'cases':len(checks),'checks':checks,'real_sd_used':False,'engine':'production file_operations.cpp'}
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({'passed':True,'cases':len(checks)}))
if __name__=='__main__':main()
