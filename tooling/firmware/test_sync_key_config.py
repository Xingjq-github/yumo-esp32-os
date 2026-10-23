"""Exercise the real CMake key generator using only synthetic keys."""
from pathlib import Path
import argparse, json, subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args()
    work=args.work.resolve()
    assert work.is_relative_to((ROOT/'artifacts/validation').resolve())
    work.mkdir(parents=True,exist_ok=False)
    source=work/'config.properties';output=work/'generated'
    script=work/'generate.cmake'
    script.write_text('cmake_minimum_required(VERSION 3.18)\n'
        f'include("{(ROOT/"firmware/cmake/vault_sync_key.cmake").as_posix()}")\n'
        f'yumo_configure_vault_sync_key("{source.as_posix()}" "{output.as_posix()}")\n',encoding='utf-8')
    cmake=Path(__import__('shutil').which('cmake') or 'cmake')
    results=[]
    def run(name,text,expected,byte=None):
        if text is not None:source.write_text(text,encoding='utf-8',newline='')
        result=subprocess.run([str(cmake),'-P',str(script)],capture_output=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        assert (result.returncode==0)==expected,name
        if expected:
            header=(output/'vault_sync_key_generated.hpp').read_text(encoding='utf-8')
            assert header.count('0x'+byte)==32,name
        results.append(name)
    run('missing_file',None,False)
    key='ab'*32
    run('bom_crlf_comments_whitespace','\ufeff# config\r\n sync.key.hex \t= '+key+' \r\n',True,'ab')
    run('changed_key_regenerates_header','sync.key.hex='+'CD'*32+'\n',True,'CD')
    for name,text in [('empty',''),('comment_only','# x\n'),('short','sync.key.hex=1234'),
                      ('long','sync.key.hex='+key+'ab'),('non_hex','sync.key.hex='+key[:-2]+'zz'),
                      ('unknown','other='+key),('duplicate','sync.key.hex='+key+'\nsync.key.hex='+key),
                      ('extra_property','sync.key.hex='+key+'\nother=1'),('semicolon','sync.key.hex='+key+';')]:
        run(name,text,False)
    report=dict(passed=True,checks=results,synthetic_only=True)
    (work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))

if __name__=='__main__':main()
