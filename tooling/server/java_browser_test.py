"""Actual shipped HTML/JS in headless Edge, isolated test server and synthetic data."""
import base64,json,os,socket,subprocess,sys,time
from pathlib import Path
import httpx,websocket
from browser_entry_views import check_entry_views
ROOT=Path(__file__).resolve().parents[2]
def keys():
    return dict(schema=2,key=os.urandom(32).hex(),token=os.urandom(32).hex(),vault_id=os.urandom(16).hex(),salt=os.urandom(16).hex())
RUN=Path(os.environ.get('VAULT_BROWSER_ROOT',ROOT/'artifacts/server/verification'/('browser-'+str(int(time.time()))))).resolve()
assert RUN.is_relative_to((ROOT/'artifacts/validation').resolve()) or RUN.is_relative_to((ROOT/'artifacts/server/verification').resolve())
RUN.mkdir(parents=True,exist_ok=False)
BROWSER=os.environ.get('VAULT_BROWSER') or str(Path(os.environ.get('ProgramFiles(x86)',os.environ.get('ProgramFiles','')))/'Microsoft/Edge/Application/msedge.exe')
def free_port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def wait_debug_port(profile,browser):
    # On Windows the file can exist while Edge still holds an exclusive handle.
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        try:
            lines=(profile/'DevToolsActivePort').read_text().splitlines()
            if lines:return int(lines[0])
        except (OSError,ValueError,IndexError):pass
        if browser.poll() is not None:raise RuntimeError('Isolated browser exited before debugger was ready')
        time.sleep(.1)
    raise RuntimeError('Isolated browser debugger startup timeout')
class CDP:
    def __init__(self,url):self.ws=websocket.create_connection(url,timeout=20,suppress_origin=True);self.id=0
    def call(self,method,params=None):
        self.id+=1;self.ws.send(json.dumps(dict(id=self.id,method=method,params=params or {})))
        while True:
            result=json.loads(self.ws.recv())
            if result.get('id')==self.id:
                if 'error'in result:raise RuntimeError(result['error'])
                return result.get('result',{})
    def evaluate(self,expression):
        r=self.call('Runtime.evaluate',dict(expression=expression,awaitPromise=True,returnByValue=True))
        if 'exceptionDetails'in r:raise AssertionError(r['exceptionDetails'])
        return r['result'].get('value')
def main():
    checks=[];browser=None;cdp=None;java_process=None
    def check(condition,name):
        if not condition:raise AssertionError(name)
        checks.append(name)
    def wait(expression):
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if cdp.evaluate(expression):return
            time.sleep(.05)
        raise AssertionError('Timeout: '+expression+' status='+str(cdp.evaluate("({url:location.href,notice:document.getElementById('notice')?.textContent,initialized:typeof initialized==='undefined'?null:initialized,epoch:typeof sessionEpoch==='undefined'?null:sessionEpoch})")))
    try:
        origin='http://127.0.0.1:'+str(free_port())
        (RUN/'keys.json').write_text(json.dumps(keys()),encoding='utf-8');(RUN/'tmp').mkdir()
        env=os.environ.copy();env.update(VAULT_PUBLIC_URL=origin,VAULT_BIND_IP='127.0.0.1',VAULT_DATA_DIR=str(RUN/'data'),VAULT_KEYS_FILE=str(RUN/'keys.json'),VAULT_CONTROL_FILE=str(RUN/'active.json'),VAULT_LOG_FILE=str(RUN/'vault.log'))
        jar=ROOT/'artifacts/server/joyvault.jar'
        with (RUN/'java-bootstrap.log').open('wb') as log:
            java_process=subprocess.Popen([os.environ['VAULT_JAVA'],'-Xmx192m','-XX:ActiveProcessorCount=2','-XX:+UseSerialGC','-Djava.io.tmpdir='+str(RUN/'tmp'),'-jar',str(jar)],env=env,cwd=jar.parents[1],stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        deadline=time.monotonic()+30;ready=False
        with httpx.Client(trust_env=False) as client:
            while time.monotonic()<deadline:
                if java_process.poll() is not None:raise RuntimeError('Java test server exited')
                try:ready=client.get(origin+'/healthz',timeout=2).status_code==200 and (RUN/'active.json').exists()
                except httpx.HTTPError:pass
                if ready:break
                time.sleep(.1)
        check(ready,'real Java HTTP server started')
        profile=RUN/'profile';profile.mkdir()
        with (RUN/'browser.log').open('wb') as log:
            browser=subprocess.Popen([BROWSER,'--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--disable-extensions','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+str(profile),'about:blank'],stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        port=wait_debug_port(profile,browser)
        with httpx.Client(trust_env=False) as client:
            targets=client.get(f'http://127.0.0.1:{port}/json/list').json()
        target=next(t for t in targets if t['type']=='page');cdp=CDP(target['webSocketDebuggerUrl'])
        cdp.call('Page.enable');cdp.call('Page.navigate',{'url':origin});wait("document.querySelector('#gateTitle')?.textContent.includes('首次')")
        check(cdp.evaluate("!document.getElementById('confirmLabel').hidden"),'first visit setup shown')
        cdp.evaluate("document.getElementById('admin').value='1234';document.getElementById('confirm').value='1234';document.getElementById('loginForm').requestSubmit()")
        wait("!document.getElementById('vault').hidden");check(cdp.evaluate("document.querySelectorAll('.category').length===4"),'four category home')
        check_entry_views(cdp,check,wait,RUN)
        cdp.evaluate("document.getElementById('add').click();document.getElementById('category').value='web';document.getElementById('category').dispatchEvent(new Event('change'))")
        check(cdp.evaluate("document.getElementById('targetLabel').textContent==='网页地址'"),'type-specific editor')
        cdp.evaluate("document.getElementById('title').value='<img src=x onerror=alert(1)>';document.getElementById('username').value='test@example.com';document.getElementById('target').value='https://example.com';document.getElementById('password').value='Synthetic-Test-Only!';document.getElementById('editForm').requestSubmit()")
        wait("!!document.getElementById('secret')");check(cdp.evaluate("document.getElementById('secret').textContent==='••••••••'"),'password hidden after save')
        check(cdp.evaluate("document.querySelectorAll('#vault img').length===0"),'title is text, not HTML')
        cdp.evaluate("document.getElementById('showSecret').click()")
        check(cdp.evaluate("document.getElementById('revealDialog').open"),'show requires administrator dialog')
        cdp.evaluate("document.getElementById('revealAdmin').value='wrong';document.getElementById('revealForm').requestSubmit()")
        wait("document.getElementById('revealError').textContent.length>0");check(cdp.evaluate("document.getElementById('secret').textContent==='••••••••'"),'wrong administrator does not reveal')
        time.sleep(2.1)  # Respect the real service failure cooldown; no test backdoor.
        cdp.evaluate("document.getElementById('revealAdmin').value='1234';document.getElementById('revealForm').requestSubmit()")
        wait("document.getElementById('secret').textContent==='Synthetic-Test-Only!'")
        check(cdp.evaluate("document.getElementById('revealAdmin').value===''"),'confirmation password promptly cleared')
        cdp.evaluate("document.getElementById('showSecret').click()")
        check(cdp.evaluate("document.getElementById('secret').textContent==='••••••••'"),'explicit hide')
        cdp.evaluate("document.querySelector('#crumb button:last-of-type').click()")
        check(cdp.evaluate("document.querySelectorAll('.entry').length===1"),'hierarchical category list')
        cdp.evaluate("document.getElementById('search').value='nomatch';document.getElementById('search').dispatchEvent(new Event('input'))")
        check(cdp.evaluate("document.querySelectorAll('.entry').length===0"),'search filters metadata')
        cdp.evaluate("document.getElementById('search').value='';document.getElementById('search').dispatchEvent(new Event('input'))")
        for width,height,name in [(1080,760,'desktop'),(390,844,'mobile')]:
            cdp.call('Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=1,mobile=width<500))
            check(cdp.evaluate('document.documentElement.scrollWidth<=window.innerWidth'),'no horizontal overflow '+name)
            check(cdp.evaluate("[...document.querySelectorAll('.entry')].every(e=>e.getBoundingClientRect().height>=42)"),'touch rows '+name)
            shot=cdp.call('Page.captureScreenshot',{'format':'png'});(RUN/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
        cdp.evaluate("document.querySelector('.entry').click();[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click()")
        check(cdp.evaluate("document.getElementById('password').value===''"),'edit does not fetch existing password')
        cdp.evaluate("document.getElementById('title').value='Website';document.getElementById('editForm').requestSubmit()")
        wait("!document.getElementById('editor').hidden===false && !!document.getElementById('secret')")
        cdp.evaluate("document.getElementById('logout').click()")
        wait("!document.getElementById('gate').hidden")
        check(cdp.evaluate("document.getElementById('content').textContent==='' && document.getElementById('password').value===''"),'logout clears view and editor')
        cdp.evaluate("document.getElementById('admin').value='1234';document.getElementById('loginForm').requestSubmit()")
        wait("!document.getElementById('vault').hidden");check(cdp.evaluate("document.querySelectorAll('.category').length===4"),'login returns to category home')
        cdp.evaluate("category='web';render();document.getElementById('importOpen').click()")
        csv_file=RUN/'web.csv'
        csv_file.write_text('url,username,password,note\nhttps://z.example,zeta,Synthetic!4,'+'long-note'*70+'\ninvalid,,,\nhttps://a.example,Alpha,Synthetic!4,\ninvalid,user,Secret中!,\n',encoding='utf-8')
        document=cdp.call('DOM.getDocument')['root']['nodeId']
        upload=cdp.call('DOM.querySelector',{'nodeId':document,'selector':'#importFile'})['nodeId']
        cdp.call('DOM.setFileInputFiles',{'nodeId':upload,'files':[str(csv_file)]})
        cdp.evaluate("document.getElementById('importForm').requestSubmit()")
        wait("!!importReady && !document.getElementById('importCommit').disabled")
        check(cdp.evaluate("document.getElementById('importSummary').textContent.includes('跳过无账号 1 行') && document.getElementById('importSummary').textContent.includes('/256')"),'CSV skip count and capacity visible')
        check(cdp.evaluate("[...document.querySelectorAll('#importRows strong')].map(e=>e.textContent).join(',')==='Alpha,zeta'"),'CSV preview account order')
        check(cdp.evaluate("document.querySelectorAll('#importSkipped li').length===1 && document.getElementById('importSkipped').textContent.includes('第 5 行') && !document.getElementById('importSkipped').textContent.includes('Secret')"),'unsupported record visibly listed without password')
        cdp.evaluate("document.getElementById('importCommit').click()")
        wait("!document.getElementById('vault').hidden && document.querySelectorAll('.entry').length===3")
        check(cdp.evaluate("items.map(e=>e.username).join(',')==='Alpha,test@example.com,zeta'"),'CSV committed account order')
        check(cdp.evaluate("items.find(e=>e.username==='zeta').note==='long-note'.repeat(70).slice(0,256)"),'long note clipped in committed browser metadata')
        cdp.evaluate("document.getElementById('importOpen').click()")
        csv_file.write_text('url,username,password\ninvalid,,\ninvalid,   ,\n',encoding='utf-8')
        cdp.call('DOM.setFileInputFiles',{'nodeId':upload,'files':[str(csv_file)]})
        cdp.evaluate("document.getElementById('importForm').requestSubmit()")
        wait("document.getElementById('importError').textContent.includes('没有可导入的记录')")
        check(cdp.evaluate("!importReady && document.getElementById('importCommit').disabled"),'all blank accounts cannot commit')
        cdp.evaluate("document.getElementById('importCancel').click()")
        # Populate the shipped service (never production data) to the real cap.
        check(cdp.evaluate("""(async()=>{
          const csv='url,username,password\\n'+Array.from({length:249},(_,i)=>`https://purpose${i}.example.test,bulk@example.com,Synthetic-Bulk-${i}!`).join('\\n');
          const preview=await api('entries/import',{category:'web',csv,preview:true});
          await api('entries/import',{category:'web',csv,preview:false,revision:preview.revision});await reload();
          for(const [username,title,target] of [['BULK@example.com','Upper','https://upper.test'],['','No account one',''],['','No account two','https://blank.test'],['<img src=x onerror=alert(1)>','Literal','https://literal.test']]){
            await api('entries',{revision,category:'web',username,title,target,note:'',password:'Synthetic-Edge!4'});await reload();
          }
          category='web';webAccount=null;selected=null;render();
          return items.length===256&&webGroups.length===7&&webByAccount.get('bulk@example.com').entries.length===249;
        })()"""),'full-capacity metadata groups exact accounts without merging credentials')
        check(cdp.evaluate("document.querySelectorAll('.entry.account').length===7 && document.querySelectorAll('#content img').length===0 && !document.getElementById('secret')"),'account screen deduplicates names and treats account text safely')
        check(cdp.evaluate("webByAccount.get('').entries.length===2 && webByAccount.get('BULK@example.com').entries.length===1"),'blank account preserved and account case not merged')
        cdp.evaluate("openAccount('bulk@example.com')")
        check(cdp.evaluate("document.querySelectorAll('.entry.purpose').length===40 && document.querySelector('.list-pages').textContent.includes('249') && !document.getElementById('secret')"),'large purpose list creates only 40 rows')
        cdp.evaluate("document.querySelector('.next-page').click();window.scrollTo(0,260);globalThis.savedListY=window.scrollY;globalThis.expectedPurpose=webByAccount.get(webAccount).entries[40];document.querySelector('.entry.purpose').click()")
        check(cdp.evaluate("selected.id===expectedPurpose.id && document.getElementById('secret').textContent==='••••••••' && document.getElementById('crumb').textContent.includes('bulk@example.com')"),'page-two purpose opens its exact original entry and masks password')
        cdp.evaluate("document.querySelector('#crumb button:last-of-type').click()")
        check(cdp.evaluate("document.querySelector('.entry.purpose').dataset.entryId===expectedPurpose.id && Math.abs(window.scrollY-savedListY)<2"),'back restores purpose page and scroll position')
        cdp.evaluate("document.querySelectorAll('#crumb button')[1].click();document.getElementById('search').value='purpose248';document.getElementById('search').dispatchEvent(new Event('input'))")
        check(cdp.evaluate("document.querySelectorAll('.entry.account').length===1"),'account search includes child purposes')
        cdp.evaluate("document.querySelector('.entry.account').click()")
        check(cdp.evaluate("document.querySelectorAll('.entry.purpose').length===1 && document.querySelector('.entry.purpose strong').textContent==='https://purpose248.example.test'"),'purpose search stays inside the selected account')
        cdp.evaluate("document.querySelector('.entry.purpose').click();[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('username').value='renamed@example.com';document.getElementById('editForm').requestSubmit()")
        wait("!!selected && selected.username==='renamed@example.com' && !document.getElementById('vault').hidden")
        check(cdp.evaluate("webAccount==='renamed@example.com' && webByAccount.get('bulk@example.com').entries.length===248 && webByAccount.get(webAccount).entries.length===1"),'editing username rebuilds groups and follows renamed account')
        cdp.evaluate("(async()=>{await api('entries/'+selected.id+'/delete',{revision});selected=null;await reload();})()")
        check(cdp.evaluate("webAccount===null && !webByAccount.has('renamed@example.com')"),'deleting last purpose returns to account list')
        cdp.evaluate("document.getElementById('search').value='';openAccount('')")
        check(cdp.evaluate("document.querySelector('.entry.purpose strong').textContent==='No account one'"),'missing purpose falls back to existing record title')
        for width,height,name in [(1080,760,'groups-desktop'),(390,844,'groups-mobile')]:
            cdp.call('Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=1,mobile=width<500))
            cdp.evaluate("openAccount('bulk@example.com')")
            check(cdp.evaluate("document.documentElement.scrollWidth<=window.innerWidth && [...document.querySelectorAll('.entry')].every(e=>e.getBoundingClientRect().height>=42)"),'grouped list fits and remains touchable '+name)
            shot=cdp.call('Page.captureScreenshot',{'format':'png'});(RUN/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
        # The real UI fills and saves the generated password; no generated value
        # is returned to the test report or the process log.
        cdp.evaluate("document.getElementById('add').click()")
        check(cdp.evaluate("document.getElementById('username').value==='bulk@example.com'"),'new purpose prefills currently selected account')
        check(cdp.evaluate("""(()=>{
          const values=new Set();
          for(let i=0;i<128;i++){
            document.getElementById('generatePassword').click();const p=document.getElementById('password').value;
            if(p.length!==16||!/[A-Z]/.test(p)||!/[a-z]/.test(p)||!/[0-9]/.test(p)||!(/[^A-Za-z0-9]/.test(p))||[...p].some(c=>c.charCodeAt(0)<33||c.charCodeAt(0)>126))return false;
            values.add(p);
          }
          globalThis.generatedForTest=document.getElementById('password').value;
          return values.size===128&&document.getElementById('password').type==='password'&&!document.body.textContent.includes(generatedForTest);
        })()"""),'recommended passwords are unique mixed printable ASCII and remain masked')
        cdp.evaluate("document.getElementById('genLength').value='8';document.getElementById('genSymbol').checked=false;document.getElementById('generatePassword').click()")
        check(cdp.evaluate("(()=>{const p=document.getElementById('password').value;return p.length===8&&!/[^A-Za-z0-9]/.test(p);})()"),'custom length 8 with symbol type unchecked is honored')
        cdp.evaluate("document.getElementById('genLength').value='12';document.getElementById('genSymbol').checked=true;document.getElementById('genUpper').checked=false;document.getElementById('genLower').checked=false;document.getElementById('genDigit').checked=false;document.getElementById('generatePassword').click()")
        check(cdp.evaluate("(()=>{const p=document.getElementById('password').value;return p.length===12&&[...p].every(c=>'!#$%&()*+,-./:;<=>?@[]^_{|}~'.includes(c));})()"),'symbol-only selection is honored')
        cdp.evaluate("document.getElementById('genLength').value='16';document.getElementById('genUpper').checked=true;document.getElementById('genLower').checked=true;document.getElementById('genDigit').checked=true;document.getElementById('genSymbol').checked=true;document.getElementById('generatePassword').click();globalThis.generatedForTest=document.getElementById('password').value")
        cdp.evaluate("document.getElementById('title').value='Generated test';document.getElementById('target').value='https://generated.test';document.getElementById('editForm').requestSubmit()")
        wait("!!selected && selected.title==='Generated test' && !document.getElementById('vault').hidden")
        check(cdp.evaluate("(async()=>{const r=await api('entries/'+selected.id+'/reveal',{password:'1234'});const ok=r.password===generatedForTest;r.password='';globalThis.generatedForTest='';return ok;})()"),'generated password survives real save and authenticated reveal unchanged')
        cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('password').value='Unchanged-Input!4'")
        check(cdp.evaluate("(()=>{const old=crypto.getRandomValues;crypto.getRandomValues=()=>{throw new Error('synthetic RNG failure')};try{document.getElementById('generatePassword').click();return document.getElementById('password').value==='Unchanged-Input!4'&&document.getElementById('notice').textContent.includes('无法生成');}finally{crypto.getRandomValues=old;}})()"),'RNG failure leaves existing input intact instead of weak fallback')
        cdp.evaluate("document.getElementById('editCancel').click()")
        # Super mode covers every credential view; all values below are synthetic.
        cdp.evaluate("openEntry(items.find(e=>e.username==='test@example.com'));document.getElementById('superMode').click()")
        check(cdp.evaluate("document.getElementById('superDialog').open && !superMode"),'super mode explicitly asks for administrator password')
        cdp.evaluate("document.getElementById('superAdmin').value='wrong';document.getElementById('superForm').requestSubmit()")
        wait("!superBusy && document.getElementById('superError').textContent.length>0")
        check(cdp.evaluate("!superMode && document.getElementById('secret').textContent==='••••••••' && document.getElementById('superAdmin').value===''"),'wrong super password leaves mode and detail masked')
        time.sleep(2.1)
        cdp.evaluate("document.getElementById('superAdmin').value='1234';document.getElementById('superForm').requestSubmit()")
        wait("superMode && !superBusy && !document.getElementById('superDialog').open")
        check(cdp.evaluate("document.getElementById('secret').textContent==='Synthetic-Test-Only!' && document.getElementById('showSecret').hidden && document.getElementById('superAdmin').value===''"),'super detail plaintext without repeated confirmation')
        check(cdp.evaluate("(()=>{hideSecret();return document.getElementById('secret').textContent==='Synthetic-Test-Only!'&&!document.getElementById('superStatus').hidden;})()"),'ordinary hide timer does not end super mode')
        cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click()")
        check(cdp.evaluate("document.getElementById('password').type==='text' && document.getElementById('password').value==='Synthetic-Test-Only!'"),'super edit displays current credential')
        cdp.evaluate("document.getElementById('title').value='Unsubmitted title';document.getElementById('superMode').click()")
        wait("!superMode&&!superBusy")
        check(cdp.evaluate("!document.getElementById('editor').hidden && document.getElementById('title').value==='Unsubmitted title' && document.getElementById('password').type==='password' && document.getElementById('password').value==='' && items.every(e=>!('password' in e))"),'exit clears cached plaintext without losing other editor fields')
        def enable_super():
            cdp.evaluate("document.getElementById('superMode').click();document.getElementById('superAdmin').value='1234';document.getElementById('superForm').requestSubmit()")
            wait("superMode && !superBusy && !document.getElementById('superDialog').open")
        enable_super()
        check(cdp.evaluate("document.getElementById('title').value==='Unsubmitted title' && document.getElementById('password').value==='Synthetic-Test-Only!'"),'enable from editor preserves unsaved fields and fills original password')
        cdp.evaluate("document.getElementById('editCancel').click();openAccount('bulk@example.com')")
        check(cdp.evaluate("document.querySelectorAll('.entry.purpose').length===40 && document.querySelectorAll('.entry.purpose .plain-password').length===40"),'super purpose list keeps 40-row bound')
        for width,height,name in [(1080,760,'super-desktop'),(390,844,'super-mobile')]:
            cdp.call('Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=1,mobile=width<500))
            check(cdp.evaluate("document.documentElement.scrollWidth<=window.innerWidth && [...document.querySelectorAll('.entry')].every(e=>e.getBoundingClientRect().height>=42)"),'super list fits and remains touchable '+name)
            shot=cdp.call('Page.captureScreenshot',{'format':'png'});(RUN/(name+'.png')).write_bytes(base64.b64decode(shot['data']))
        cdp.evaluate("category=null;webAccount=null;document.getElementById('search').value='test@example.com';render()")
        check(cdp.evaluate("document.querySelector('.entry .plain-password').textContent==='Synthetic-Test-Only!'"),'global search displays plaintext in super mode')
        cdp.evaluate("document.getElementById('search').value='';category='web';render();document.getElementById('add').click();document.getElementById('generatePassword').click()")
        check(cdp.evaluate("document.getElementById('password').type==='text' && document.getElementById('password').value.length===16"),'generated new password is visible in super mode')
        cdp.evaluate("document.getElementById('editCancel').click();document.getElementById('importOpen').click()")
        super_csv=RUN/'super.csv';super_csv.write_text('url,username,password\nhttps://example.com,test@example.com,Synthetic-Test-Only!\n',encoding='utf-8')
        cdp.call('DOM.setFileInputFiles',{'nodeId':upload,'files':[str(super_csv)]})
        cdp.evaluate("document.getElementById('importForm').requestSubmit()")
        wait("!!importReady && !document.getElementById('importCommit').disabled")
        check(cdp.evaluate("document.querySelector('#importRows .plain-password').textContent==='Synthetic-Test-Only!'"),'CSV preview displays plaintext only when elevated')
        cdp.evaluate("document.getElementById('superMode').click()")
        wait("!superMode&&!superBusy")
        check(cdp.evaluate("!document.getElementById('importer').hidden && !document.querySelector('#importRows .plain-password') && !!importReady"),'exit masks CSV preview and preserves pending import')
        enable_super();wait("!!document.querySelector('#importRows .plain-password')")
        cdp.evaluate("document.getElementById('importCancel').click();document.getElementById('archiveOpen').click()")
        cdp.evaluate("(async()=>{const response=await fetch('/api/archive/export',{method:'POST'});const transfer=new DataTransfer();transfer.items.add(new File([await response.blob()],'synthetic.jyvault'));const input=document.getElementById('archiveFile');input.files=transfer.files;input.dispatchEvent(new Event('change'));document.getElementById('archiveForm').requestSubmit();})()")
        wait("!!archiveReady && !archiveBusy")
        check(cdp.evaluate("document.querySelectorAll('#archiveRows .plain-password').length===new Set(items.map(e=>JSON.stringify([e.category,e.username,e.password]))).size && document.querySelectorAll('#archiveRows .plain-password').length===255"),'encrypted import preview displays every deduplicated credential in super mode')
        cdp.evaluate("document.getElementById('superMode').click()")
        wait("!superMode&&!superBusy")
        check(cdp.evaluate("!document.querySelector('.plain-password') && !!archiveReady && items.every(e=>!('password' in e))"),'exit removes all preview and metadata plaintext')
        cdp.evaluate("document.getElementById('archiveCancel').click()")
        enable_super()
        cdp.evaluate("(async()=>{await api('super-mode',{enabled:false});const channel=new BroadcastChannel('joyvault-session');channel.postMessage('normal');channel.close();})()")
        wait("!superMode")
        check(cdp.evaluate("items.every(e=>!('password' in e)) && !document.querySelector('.plain-password')"),'another tab mode-exit signal clears plaintext immediately')
        enable_super()
        check(cdp.evaluate("(async()=>{const pending=reload().then(()=>false).catch(e=>!!e.stale);await exitSuperMode();return await pending;})()"),'late plaintext response cannot restore mode after exit')
        check(cdp.evaluate("(async()=>{const data=await api('entries');data.super_mode=true;for(const item of data.entries)item.password='Stale-Response!9';receiveEntries(data);render();return !superMode&&items.every(e=>!('password' in e))&&!document.querySelector('.plain-password');})()"),'stale privileged metadata cannot implicitly re-enable local mode')
        enable_super()
        cdp.evaluate("document.getElementById('logout').click()")
        wait("!document.getElementById('gate').hidden")
        check(cdp.evaluate("!superMode && items.length===0 && document.getElementById('password').value==='' && !document.querySelector('.plain-password') && document.getElementById('superMode').hidden"),'lock purges plaintext and privileged controls')
        cdp.evaluate("document.getElementById('admin').value='1234';document.getElementById('loginForm').requestSubmit()")
        wait("!document.getElementById('vault').hidden")
        check(cdp.evaluate("!superMode && items.every(e=>!('password' in e))"),'new login returns to ordinary masked mode')
        enable_super()
        cdp.evaluate("lastActivity=Date.now()-310000")
        time.sleep(.2);wait("!document.getElementById('gate').hidden")
        check(cdp.evaluate("!superMode&&items.length===0"),'automatic idle lock also revokes super mode')
        cdp.evaluate("document.getElementById('admin').value='1234';document.getElementById('loginForm').requestSubmit()")
        wait("!document.getElementById('vault').hidden")
        stale=cdp.evaluate("(async()=>{const p=reload().then(()=>false).catch(e=>!!e.stale);gate();return await p;})()")
        check(stale and cdp.evaluate("!document.getElementById('gate').hidden && document.getElementById('content').textContent===''"),'late response cannot reopen locked view')
        check(cdp.evaluate("webGroups.length===0 && webByAccount.size===0 && appGroups.length===0 && appByName.size===0 && appName===null && workGroups.length===0 && workByName.size===0 && workName===null && document.getElementById('targetOptions').children.length===0 && searchById.size===0 && listViews.size===0 && webAccount===null"),'lock releases grouping search and list-position caches')
        report={'passed':True,'checks':checks,'run':str(RUN)}
    except Exception as exc:
        report={'passed':False,'checks':checks,'error':repr(exc),'run':str(RUN)}
        raise
    finally:
        (RUN/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False))
        if cdp:
            try:cdp.call('Browser.close')
            except Exception:pass
            cdp.ws.close()
        if browser:
            try:browser.wait(timeout=10)
            except subprocess.TimeoutExpired:browser.terminate();browser.wait(timeout=5)
        if java_process and java_process.poll() is None:
            try:
                state=json.loads((RUN/'active.json').read_text());assert state['pid']==java_process.pid
                with socket.create_connection(('127.0.0.1',state['port']),3) as client:
                    client.sendall(('STOP '+state['token']+'\n').encode());assert client.recv(32).startswith(b'OK')
                java_process.wait(timeout=30)
            except Exception:java_process.terminate();java_process.wait(timeout=10)
    return 0
if __name__=='__main__':raise SystemExit(main())
