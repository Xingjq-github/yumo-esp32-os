"""Shipped LAN HTML in headless Edge with isolated, synthetic HTTP fixtures."""
from pathlib import Path
import argparse,base64,hashlib,json,os,subprocess,sys,threading,time
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
from urllib.parse import urlparse,parse_qs
ROOT=Path(__file__).resolve().parents[2]
import httpx,websocket
class CDP:
    def __init__(self,url):self.ws=websocket.create_connection(url,timeout=20,suppress_origin=True);self.id=0
    def call(self,method,params=None):
        self.id+=1;self.ws.send(json.dumps(dict(id=self.id,method=method,params=params or {})))
        while True:
            result=json.loads(self.ws.recv())
            if result.get('id')==self.id:
                if 'error'in result:raise RuntimeError(result['error'])
                return result.get('result',{})
    def evaluate(self,expr):
        result=self.call('Runtime.evaluate',dict(expression=expr,awaitPromise=True,returnByValue=True))
        if 'exceptionDetails'in result:raise AssertionError(result['exceptionDetails'])
        return result['result'].get('value')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run-dir',type=Path,required=True);run=parser.parse_args().run_dir.resolve()
    assert run.is_relative_to((ROOT/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    files={f'/file-{i:02}.txt':b'synthetic' for i in range(36)};files['/sample.txt']=b'download';folders={'/Pictures'};checks=[]
    for name in ('a-photo.JPG','b-song.mp3','c-movie.mp4','d-backup.zip','e-source.cpp','f-unknown.bin','Pictures/sunset.jpg'):files['/'+name]=b'synthetic'
    entries=[dict(title='<img src=x onerror=alert(1)>',category='work',username='account',password='Synthetic-Secret!',target='Unit A',note='note\nline2',text='Synthetic details'),dict(title='Other unit',category='work',username='other',password='Test',target='Unit B',note='',text='Other details'),dict(title='Chat app',category='app',username='chat',password='AppSecret',target='Messenger',note='',text='App details'),dict(title='Code',category='other',password='123456',target='Door',note='',text='Code details')]
    def entry(category,title,target='',username='user'):
        return dict(category=category,title=title,target=target,username=username,password='Fixture-Only',note='',text='Synthetic '+title)
    entries.extend([
        entry('work','审批系统','Unit A'),entry('work','旧版未填写单位'),entry('work','名称恰好是未填写','未填写'),
        entry('app','备用账号','Messenger'),entry('app','影音','Media'),
        entry('web','论坛登录','论坛','User@example.test'),entry('web','商城登录','购物','User@example.test'),
        entry('web','区分大小写的账号','阅读','user@example.test'),entry('web','未填写账号的用途','工具','')
    ])
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,body,ctype='application/json',cookie=None):
            raw=body if isinstance(body,bytes) else json.dumps(body).encode();self.send_response(status);self.send_header('Content-Type',ctype);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(raw)))
            if cookie:self.send_header('Set-Cookie',cookie)
            self.end_headers();self.wfile.write(raw)
        def do_GET(self):
            parts=urlparse(self.path);q=parse_qs(parts.query);path=q.get('path',['/'])[0]
            if parts.path=='/':return self.reply(200,(ROOT/'firmware/main/core/lan_web.html').read_bytes(),'text/html; charset=utf-8')
            if parts.path=='/api/files':
                parent=lambda p:p.rsplit('/',1)[0] or '/'
                data=[dict(name=p.rsplit('/',1)[1],directory=True,bytes=0) for p in sorted(folders) if parent(p)==path]+[dict(name=p.rsplit('/',1)[1],directory=False,bytes=len(data)) for p,data in sorted(files.items()) if parent(p)==path];offset=int(q.get('offset',['0'])[0]);return self.reply(200,dict(entries=data[offset:offset+32],more=len(data)>offset+32))
            if parts.path=='/api/passwords':return self.reply(200,entries) if 'yumo_session=test' in self.headers.get('Cookie','') else self.reply(401,b'locked','text/plain')
            if parts.path=='/api/download':return self.reply(200,files[path],'application/octet-stream')
            self.reply(404,b'missing','text/plain')
        def do_POST(self):
            parts=urlparse(self.path);q=parse_qs(parts.query);body=self.rfile.read(int(self.headers.get('Content-Length',0)))
            if parts.path=='/api/login':return self.reply(200,{},cookie='yumo_session=test; HttpOnly; SameSite=Strict; Path=/') if body==b'1234' else self.reply(401,'管理密码错误'.encode(),'text/plain; charset=utf-8')
            if parts.path=='/api/logout':return self.reply(200,{},cookie='yumo_session=; Path=/; Max-Age=0')
            if parts.path=='/api/files':
                path=q['path'][0]
                if q['action'][0]=='mkdir':folders.add(path)
                else:files[q['target'][0]]=files.pop(path)
                return self.reply(200,{})
            self.reply(404,{})
        def do_PUT(self):
            path=parse_qs(urlparse(self.path).query)['path'][0];data=self.rfile.read(int(self.headers.get('Content-Length',0)))
            if path in files:return self.reply(409,b'exists','text/plain')
            files[path]=data;self.reply(200,dict(bytes=len(data)-1 if 'mismatch' in path else len(data)))
        def do_DELETE(self):
            path=parse_qs(urlparse(self.path).query)['path'][0];files.pop(path,None);folders.discard(path);self.reply(200,{})
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start();browser=None;cdp=None
    def check(condition,name):
        if not condition:raise AssertionError(name)
        checks.append(name)
    def wait(expr):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if cdp.evaluate(expr):return
            time.sleep(.05)
        raise AssertionError('Timeout '+expr)
    def click(selector):cdp.evaluate('document.querySelector('+json.dumps(selector)+').click()')
    def screenshot(label,width=390,height=844):
        cdp.call('Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=1,mobile=width<500));time.sleep(.1)
        check(cdp.evaluate('document.documentElement.scrollWidth<=innerWidth'),'no horizontal overflow '+label)
        (run/(label+'.png')).write_bytes(base64.b64decode(cdp.call('Page.captureScreenshot')['data']))
    def unlock():
        cdp.evaluate("document.getElementById('pin').value='1234';document.getElementById('login').requestSubmit()")
        wait("!document.getElementById('unlocked').hidden")
    try:
        profile=run/'profile';profile.mkdir();edge=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft/Edge/Application/msedge.exe'
        log=(run/'edge.log').open('wb');browser=subprocess.Popen([str(edge),'--headless=new','--disable-gpu','--no-first-run','--disable-background-networking','--disable-sync','--disable-extensions','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+str(profile),'about:blank'],stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        deadline=time.monotonic()+25;port=None
        while time.monotonic()<deadline:
            try:port=int((profile/'DevToolsActivePort').read_text().splitlines()[0]);break
            except (OSError,ValueError,IndexError):time.sleep(.1)
        assert port
        with httpx.Client(trust_env=False) as client:targets=client.get(f'http://127.0.0.1:{port}/json/list').json()
        cdp=CDP(next(t for t in targets if t['type']=='page')['webSocketDebuggerUrl']);cdp.call('Page.enable');cdp.call('Page.navigate',dict(url=f'http://127.0.0.1:{server.server_port}'))
        wait("document.querySelectorAll('.file').length===32&&!busy")
        check(cdp.evaluate("document.getElementById('vault').hidden&&records.length===0"),'starts with files and no password cache')
        check(cdp.evaluate("['folder','image','audio','video','archive','code','file','text'].every(kind=>document.querySelector('.file [data-icon='+kind+'] svg path'))"),'folder and file types use embedded vector artwork')
        check(cdp.evaluate("[...document.querySelectorAll('.file .icon svg')].every(s=>s.namespaceURI==='http://www.w3.org/2000/svg'&&s.getBoundingClientRect().width===25&&s.getBoundingClientRect().height===25)&&document.querySelectorAll('script[src],link[rel=stylesheet],svg image,svg use').length===0"),'icons have stable size and no external assets')
        screenshot('files-desktop',1080,900);screenshot('files-mobile');screenshot('files-narrow',320)
        click('.file .name button');wait("current==='/Pictures'&&!busy")
        check(cdp.evaluate("document.querySelectorAll('.file').length===1&&document.querySelector('.file .icon').dataset.icon==='image'"),'folder navigation retains correct child image icon')
        click('#up');wait("current==='/'&&!busy")
        cdp.evaluate("document.getElementById('next').click()");wait("offset===32&&!busy");check(cdp.evaluate("document.querySelectorAll('.file').length===12"),'bounded pagination')
        cdp.evaluate("document.getElementById('prev').click()");wait("offset===0&&!busy")
        check(cdp.evaluate("validName('中文 文件.txt')&&!validName('../escape')&&!validName('bad/name')"),'filename validation')
        cdp.evaluate("window.prompt=()=> 'Folder';document.getElementById('mkdir').click()");wait("!busy&&document.getElementById('files').textContent.includes('Folder')");check('/Folder'in folders,'mkdir HTTP operation')
        cdp.evaluate("sendFile(new File(['abc\\n123'],'uploaded.txt')).then(()=>true)");check(files['/uploaded.txt']==b'abc\n123','binary upload and server byte confirmation')
        check(cdp.evaluate("sendFile(new File(['abc'],'mismatch.txt')).then(()=>false).catch(e=>e.message.includes('字节数'))"),'mismatched upload rejected')
        cdp.evaluate("window.prompt=()=> 'renamed.txt';rename('/uploaded.txt','uploaded.txt')");check('/renamed.txt'in files and '/uploaded.txt'not in files,'rename operation')
        cdp.evaluate("window.confirm=()=>true;remove('/renamed.txt',false)");check('/renamed.txt'not in files,'confirmed delete operation')
        cdp.evaluate("document.getElementById('vaultTab').click();document.getElementById('pin').value='9999';document.getElementById('login').requestSubmit()");wait("document.getElementById('status').textContent.includes('错误')");check(cdp.evaluate("records.length===0&&document.getElementById('pin').value===''"),'bad PIN never reveals entries')
        unlock()
        check(cdp.evaluate("[...document.querySelectorAll('.category-button strong')].map(n=>n.textContent).join(',')==='工作,应用,网页,其他'&&document.querySelectorAll('.card,.group-button').length===0&&document.getElementById('vaultSearch').hidden"),'unlock starts at four categories without credential details')
        check(cdp.evaluate("document.querySelector('[data-category=work] small').textContent.endsWith('5 条')&&document.getElementById('vaultCount').textContent==='13 条'"),'category counts match records')
        screenshot('categories-desktop',1080,800);screenshot('categories-mobile');screenshot('categories-narrow',320)
        click('[data-category=work]')
        check(cdp.evaluate("document.querySelectorAll('.group-button').length===4&&document.querySelectorAll('.card').length===0&&document.getElementById('vaultTitle').textContent==='单位 / 系统'"),'work first lists exact unit and system groups')
        check(cdp.evaluate("[...document.querySelectorAll('.group-button')].some(n=>n.dataset.group==='')&&[...document.querySelectorAll('.group-button')].some(n=>n.dataset.group==='未填写')"),'missing target stays distinct from literal placeholder group')
        screenshot('groups-mobile')
        cdp.evaluate("document.getElementById('search').value='审批';document.getElementById('search').dispatchEvent(new Event('input'))")
        check(cdp.evaluate("document.querySelectorAll('.group-button').length===1&&document.querySelector('.group-button').dataset.group==='Unit A'&&document.querySelectorAll('.card').length===0"),'group search finds matching metadata without displaying credentials')
        click('.group-button')
        check(cdp.evaluate("document.querySelectorAll('.card').length===2&&vaultGroup==='Unit A'&&document.getElementById('search').value===''"),'selected work group opens only its records and clears group search')
        check(cdp.evaluate("document.querySelectorAll('#entries img').length===0&&document.getElementById('entries').textContent.includes('<img')"),'metadata rendered as text without HTML execution')
        cdp.evaluate("Object.defineProperty(navigator,'clipboard',{value:null,configurable:true});document.execCommand=()=>{window.copied=document.querySelector('textarea').value;return true};document.querySelector('.field button').click()")
        check(cdp.evaluate("window.copied==='account'&&!document.querySelector('textarea')"),'HTTP clipboard fallback copies and clears temporary textarea')
        screenshot('details-desktop',1080,900);screenshot('details-mobile')
        cdp.evaluate("document.getElementById('search').value='Other unit';document.getElementById('search').dispatchEvent(new Event('input'))")
        check(cdp.evaluate("document.querySelectorAll('.card').length===0&&document.getElementById('entries').textContent.includes('没有符合条件')"),'record search remains inside selected group')
        click('#vaultBack');check(cdp.evaluate("vaultCategory==='work'&&vaultGroup===null&&document.querySelectorAll('.group-button').length===4"),'back returns to group selection')
        click('.group-button[data-group=""]');check(cdp.evaluate("document.querySelectorAll('.card').length===1&&document.getElementById('entries').textContent.includes('旧版未填写单位')"),'empty target records remain reachable')
        click('#vaultBack');click('.group-button[data-group="未填写"]');check(cdp.evaluate("document.querySelectorAll('.card').length===1&&document.getElementById('entries').textContent.includes('名称恰好是未填写')"),'literal placeholder does not merge with absent group')
        click('#vaultCrumbs .crumb');check(cdp.evaluate("document.querySelectorAll('.category-button').length===4&&document.querySelectorAll('.card').length===0"),'home breadcrumb returns to four clean categories')
        click('[data-category=other]')
        check(cdp.evaluate("document.getElementById('entries').textContent.includes('码值')&&document.getElementById('entries').textContent.includes('123456')&&!document.getElementById('entries').textContent.includes('账号')"),'other displays code and omits account')
        check(cdp.evaluate("document.querySelectorAll('.group-button').length===0&&document.querySelectorAll('.card').length===1"),'other uses direct records like device app')
        click('#vaultBack');click('[data-category=app]')
        check(cdp.evaluate("document.querySelectorAll('.group-button').length===2&&document.getElementById('vaultTitle').textContent==='应用名称 / 平台'"),'app first groups by name and platform')
        click('.group-button[data-group="Messenger"]');check(cdp.evaluate("document.querySelectorAll('.card').length===2"),'app platform opens only its accounts')
        click('#vaultCrumbs .crumb');click('[data-category=web]')
        check(cdp.evaluate("document.querySelectorAll('.group-button').length===3&&document.querySelectorAll('.card').length===0&&document.getElementById('vaultTitle').textContent==='网页账号'"),'web groups by exact account including case and absent account')
        click('.group-button[data-group="User@example.test"]')
        check(cdp.evaluate("document.querySelectorAll('.card').length===2&&document.getElementById('vaultTitle').textContent==='用途列表'&&document.getElementById('entries').textContent.includes('用途：购物')"),'web account opens its usage list')
        screenshot('web-details-mobile');click('#vaultBack');click('.group-button[data-group=""]')
        check(cdp.evaluate("document.querySelectorAll('.card').length===1&&document.getElementById('vaultCrumbs').textContent.includes('未填写账号')"),'web missing account remains reachable')
        # Empty categories remain usable and do not retain previous cards.
        cdp.evaluate("window.savedEntries=records;records=records.filter(e=>e.category!=='app')");click('#vaultCrumbs .crumb');click('[data-category=app]')
        check(cdp.evaluate("document.querySelectorAll('.group-button,.card').length===0&&document.getElementById('entries').textContent.includes('暂无记录')"),'empty category has a clear empty state')
        cdp.evaluate("records=window.savedEntries;delete window.savedEntries");click('#vaultBack');click('[data-category=web]');click('.group-button[data-group="User@example.test"]')
        cdp.evaluate("document.getElementById('diskTab').click()");check(cdp.evaluate("records.length===0&&vaultCategory===null&&vaultGroup===null&&['entries','vaultChoices','vaultCrumbs','vaultTitle','vaultCount'].every(id=>document.getElementById(id).childElementCount===0&&document.getElementById(id).textContent==='')&&document.getElementById('unlocked').hidden"),'navigation wipes plaintext, group accounts and state')
        click('#vaultTab');unlock();click('[data-category=other]');click('#lock')
        check(cdp.evaluate("records.length===0&&document.getElementById('entries').textContent===''&&document.getElementById('unlocked').hidden"),'explicit lock clears rendered codes')
        cdp.evaluate("window.nativeSetTimeout=window.setTimeout;window.setTimeout=(fn,ms)=>ms===290000?(window.expireVault=fn,0):nativeSetTimeout(fn,ms)");unlock();click('[data-category=other]')
        cdp.evaluate("window.expireVault();window.setTimeout=window.nativeSetTimeout;delete window.expireVault")
        check(cdp.evaluate("records.length===0&&document.getElementById('entries').textContent===''&&document.getElementById('status').textContent.includes('到期')"),'session expiry still removes all displayed credentials')
        # Delay a password response, leave the page, then deliver it. Its epoch
        # must be rejected even though the underlying HTTP request succeeded.
        cdp.evaluate("window.originalFetch=fetch;window.fetch=(u,o)=>u==='/api/passwords'?new Promise(resolve=>window.deliver=()=>resolve(new Response(JSON.stringify([{category:'work',title:'late',password:'late'}]),{status:200}))):originalFetch(u,o);tab(true);document.getElementById('pin').value='1234';document.getElementById('login').requestSubmit()")
        wait("typeof window.deliver==='function'");cdp.evaluate("tab(false);window.deliver()");time.sleep(.2)
        check(cdp.evaluate("records.length===0&&document.getElementById('entries').childElementCount===0"),'late password response cannot reopen a locked page')
        report=dict(passed=True,checks=checks,real_device=False,synthetic_http=True,inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ('firmware/main/core/lan_web.html','tooling/firmware/test_lan_web_browser.py')})
        (run/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(f'{len(checks)} browser checks passed')
    finally:
        if cdp:cdp.ws.close()
        if browser:browser.terminate();browser.wait(timeout=10)
        server.shutdown();server.server_close()
if __name__=='__main__':main()
