"""Java HTTP/protocol integration, synthetic keys and isolated data only."""
import argparse,base64,csv,datetime,hashlib,http.cookiejar,io,json,os,socket,subprocess,sys,time,urllib.request,urllib.error
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
SERVER=Path(__file__).resolve().parents[2]/'server'
from protocol_bridge import ProtocolBridge

def run(java,jar,work):
    jar=str(Path(jar).resolve())
    work=Path(work).resolve();project=SERVER.parent
    assert (work.is_relative_to(project/'artifacts/validation') or work.is_relative_to(SERVER.parent/'artifacts/server/verification')) and not work.exists(), 'Fresh isolated work directory required'
    bridge=ProtocolBridge(java,SERVER);encode=bridge.encode;decode=bridge.decode
    work.mkdir(parents=True);(work/'tmp').mkdir()
    keys=dict(schema=2,key=os.urandom(32).hex(),token=os.urandom(32).hex(),vault_id=os.urandom(16).hex(),salt=os.urandom(16).hex())
    (work/'keys.json').write_text(json.dumps(keys),encoding='utf-8')
    with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
    origin=f'http://127.0.0.1:{port}';control=work/'active.json';checks=[];process=None;output=None
    env=os.environ.copy();env.update(VAULT_PUBLIC_URL=origin,VAULT_BIND_IP='127.0.0.1',VAULT_DATA_DIR=str(work/'data'),VAULT_KEYS_FILE=str(work/'keys.json'),VAULT_CONTROL_FILE=str(control),VAULT_LOG_FILE=str(work/'vault.log'))
    args=[java,'-Xms32m','-Xmx192m','-Xss512k','-XX:MaxMetaspaceSize=96m','-XX:ReservedCodeCacheSize=32m','-XX:MaxDirectMemorySize=16m','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-XX:+ExitOnOutOfMemoryError','-Dfile.encoding=UTF-8','-Duser.timezone=Asia/Shanghai','-Djava.io.tmpdir='+str(work/'tmp'),'-jar',jar]
    def client():return urllib.request.build_opener(urllib.request.ProxyHandler({}),urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    browser=client()
    def request(path,body=None,method=None,agent=None,headers=None,raw=None):
        h={'Origin':origin};h.update(headers or {})
        data=raw
        if body is not None:data=json.dumps(body,ensure_ascii=False).encode();h['Content-Type']='application/json'
        req=urllib.request.Request(origin+path,data=data,headers=h,method=method or ('POST' if data is not None else 'GET'))
        try:
            with (agent or browser).open(req,timeout=15) as response:return response.status,response.read(),response.headers
        except urllib.error.HTTPError as e:return e.code,e.read(),e.headers
    def check(ok,name):
        if not ok:raise AssertionError(name)
        checks.append(name)
    def obj(response,status=200):
        assert response[0]==status,(response[0],response[1][:220]);return json.loads(response[1])
    def start():
        nonlocal process,output
        output=(work/('bootstrap-'+str(time.time_ns())+'.log')).open('wb')
        process=subprocess.Popen(args,cwd=SERVER,env=env,stdout=output,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if process.poll() is not None:raise RuntimeError('Java startup failed; inspect isolated log')
            try:
                if request('/healthz')[0]==200 and control.exists():return
            except (OSError,urllib.error.URLError):pass
            time.sleep(.15)
        raise TimeoutError('Java health startup')
    def stop():
        nonlocal process,output
        if process is None:return
        if process.poll() is None:
            data=json.loads(control.read_text());assert data['pid']==process.pid
            with socket.create_connection(('127.0.0.1',data['port']),3) as connection:
                connection.sendall(('STOP '+data['token']+'\n').encode());assert connection.recv(32).startswith(b'OK')
            process.wait(timeout=30)
        if output:output.close()
        process=None
    def entries():return obj(request('/api/entries'))
    def verify_process_identity():
        state=json.loads(control.read_text());check(state['pid']==process.pid,'Java 8 local control PID identity')
        if os.name=='nt':
            import ctypes
            from ctypes import wintypes
            kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            kernel.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
            kernel.GetProcessTimes.restype=wintypes.BOOL
            stamps=[wintypes.FILETIME() for _ in range(4)]
            if not kernel.GetProcessTimes(wintypes.HANDLE(int(process._handle)),*[ctypes.byref(stamp) for stamp in stamps]):raise ctypes.WinError(ctypes.get_last_error())
            created=((stamps[0].dwHighDateTime<<32)|stamps[0].dwLowDateTime)/10_000_000-11644473600
            recorded=datetime.datetime.fromisoformat(state['started'].replace('Z','+00:00')).timestamp()
            check(abs(created-recorded)<=2,'Java 8 JVM start time matches Windows process within stop-script tolerance')
    def add(**extra):
        value=dict(title='合成测试',category='work',username='test@example.com',password='Test-Only!4',target='local',note='',revision=entries()['revision']);value.update(extra)
        return obj(request('/api/entries',value))
    proof=bridge.call('sync-proof',keys)
    device_headers={'Authorization':'Bearer '+keys['token'],'X-Vault-Key':proof}
    try:
        start();check(obj(request('/healthz'))=={'status':'ok','protocol':2},'health contract');verify_process_identity()
        probe=[java,'-Xms8m','-Xmx32m','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-XX:ReservedCodeCacheSize=16m','-jar',jar,'healthcheck']
        result=subprocess.run(probe,env=dict(env,VAULT_HEALTH_URL=origin),capture_output=True,timeout=8,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        check(result.returncode==0 and result.stdout.strip()==b'healthy','packaged Java 8 health CLI with NAS memory limits')
        result=subprocess.run(probe,env=dict(env,VAULT_HEALTH_URL=origin+'/invalid'),capture_output=True,timeout=8,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        check(result.returncode==1 and result.stdout.strip()==b'unhealthy','packaged Java 8 health CLI failure exit code')
        check(not obj(request('/api/status'))['initialized'],'new Java database not initialized')
        check(request('/api/entries')[0]==401,'anonymous list denied')
        check(request('/api/device/bundle')[0]==401,'device requires bearer')
        for path in ('/api/device/bundle','/api/device/manifest'):
            check(request(path,headers={'Authorization':'Bearer '+keys['token']})[0]==403,'missing compiled sync key denied '+path)
            check(request(path,headers=dict(device_headers,**{'X-Vault-Key':'0'*64}))[0]==403,'wrong compiled sync key denied '+path)
        check(request('/api/device/manifest',headers=device_headers)[0]==404,'uninitialized manifest is 404')
        for path in ['/api/devices','/api/vault','/crypto.js','/docs']:
            check(request(path)[0]==404,'legacy route absent '+path)
        check(request('/api/setup',{'password':'123','confirm':'123'})[0]==400,'minimum admin length')
        first=request('/api/setup',{'password':'1234','confirm':'1234'});obj(first)
        check('HttpOnly' in first[2]['Set-Cookie'] and 'SameSite=strict' in first[2]['Set-Cookie'] and 'Secure' not in first[2]['Set-Cookie'],'cookie contract')
        check(request('/api/setup',{'password':'other','confirm':'other'})[0]==409,'no admin overwrite')
        for path in ['/','/app.js','/style.css']:
            response=request(path);name={'/':'index.html','/app.js':'app.js','/style.css':'style.css'}[path]
            check(response[0]==200 and response[1]==(SERVER/'src/main/resources/static'/name).read_bytes(),'unchanged page '+name)
            check(response[2]['Cache-Control']=='no-store','no-store '+name)
        saved=add();identifier=saved['entry']['id']
        synced=request('/api/device/bundle',headers=device_headers)
        check(synced[0]==200 and synced[2].get('X-Vault-Key')==proof,'server acknowledges matching compiled sync key')
        check('password' not in saved['entry'] and 'password' not in entries()['entries'][0],'metadata hides password')
        check(request('/api/entries',dict(title='x',password='x',category='work',revision=True))[0]==409,'boolean revision rejected')
        check(request('/api/entries',dict(title='x',password='x',category='work',revision=1))[0]==409,'stale revision rejected')
        update=add(id=identifier,password='',note='changed');check(update['entry']['note']=='changed','update metadata')
        large_note='"'*4096
        update=add(id=identifier,password='',note=large_note);check(update['entry']['note']==large_note[:256],'long note is clipped to 256 bytes through real HTTP')
        add(id=identifier,password='',note='changed')
        check(obj(request('/api/entries/'+identifier+'/reveal',{'password':'1234'}))['password']=='Test-Only!4','empty edit retains password')
        for field,value in [('password','中文'),('username','中'),('title',' '),('note','bad\nnote'),('password','x'*129),('target','x'*193)]:
            payload=dict(title='test',password='ok',category='work',revision=entries()['revision']);payload[field]=value
            check(request('/api/entries',payload)[0]==400,'field validation '+field+str(len(checks)))
        check(request('/api/login',{'password':'1234'},headers={'Origin':'http://other.local'})[0]==403,'origin enforced')
        check(request('/api/login',raw=b'x'*9000)[0]==413,'bounded ordinary body')
        check(request('/api/entries',raw=b'[]')[0]==400,'JSON object required')
        check(request('/api/entries/import')[0]==405,'CSV method contract')
        check(request('/api/archive/export')[0]==405,'export method contract')
        check(request('/api/entries/import',dict(csv='',category='work',preview=True))[0]==400,'CSV web only')
        text='name,url,username,password,note\r\nfirst,https://first.example/a,web-user,Web-Test!4,keep\r\nsecond,https://second.example/b,web-user,Web-Test!4,\r\nthird,https://second.example/c,web-user,Web-Test!4,\r\n'
        text+='ignored,invalid,,bad,\r\nignored,invalid,   ,bad,\r\n'
        text+='ignored,invalid,user,Secret中!,\r\n'
        before=entries()['revision'];preview=obj(request('/api/entries/import',dict(csv=text,category='web',preview=True)))
        check(preview['skipped']==3 and preview['skipped_empty']==2 and len(preview['skipped_entries'])==1 and preview['error_count']==0,'CSV skips blank and unsupported records before validation')
        check(preview['skipped_entries'][0]['row']==7 and 'Secret' not in json.dumps(preview),'skipped record reports line without exposing password')
        check(preview['added']==1 and preview['merged']==2 and not preview['committed'] and entries()['revision']==before,'CSV preview no write')
        check('password' not in preview['entries'][0] and preview['entries'][0]['title']=='web-user' and preview['entries'][0]['note']=='keep,second.exa','CSV naming shortened domains and masking')
        commit=obj(request('/api/entries/import',dict(csv=text,category='web',preview=False,revision=before)))
        repeat=obj(request('/api/entries/import',dict(csv=text,category='web',preview=False,revision=commit['revision'])))
        check(repeat['revision']==commit['revision'] and repeat['unchanged']==1,'CSV dedup idempotence')
        bundle=request('/api/device/bundle',headers=device_headers);manifest=obj(request('/api/device/manifest',headers=device_headers));decoded=decode(bundle[1],keys)
        check(len(decoded)==2 and manifest['sha256']==hashlib.sha256(bundle[1]).hexdigest() and manifest['size']==len(bundle[1]),'Java HTTP snapshot decoded with test bridge and manifest')
        check(bundle[2]['ETag']==str(manifest['revision']),'device ETag contract')
        check(b'Test-Only!4' not in bundle[1],'sync has no plaintext password')
        exported=request('/api/archive/export',{},method='POST')
        check(exported[1]==bundle[1] and 'attachment;' in exported[2]['Content-Disposition'],'export exact snapshot')
        base64file=base64.b64encode(bundle[1]).decode();rev=entries()['revision']
        repeat=obj(request('/api/archive/import',dict(file=base64file,preview=False,revision=rev)))
        check(repeat['revision']==rev and repeat['unchanged']==2,'archive no-op no new revision')
        incoming=[dict(decoded[0],title='额外名称',target='other',note='more',favorite=True),dict(decoded[0],category='app')]
        imported=base64.b64encode(encode(incoming,100,keys)).decode()
        preview=obj(request('/api/archive/import',dict(file=imported,preview=True)))
        check(preview['added']==1 and preview['updated']==1 and 'password' not in preview['entries'][0],'archive merge preview')
        commit=obj(request('/api/archive/import',dict(file=imported,preview=False,revision=rev)))
        check(commit['revision']==rev+1 and commit['source_revision']==100,'Test bridge encode -> HTTP import; local revision monotonic')
        check(request('/api/archive/import',dict(file=imported,preview=False,revision=rev))[0]==409,'stale archive preview rejected')
        bad=bytearray(bundle[1]);bad[-1]^=1
        check(request('/api/archive/import',dict(file=base64.b64encode(bad).decode(),preview=False,revision=commit['revision']))[0]==400 and entries()['revision']==commit['revision'],'tamper atomic rejection')
        other=client();obj(request('/api/login',{'password':'1234'},agent=other))
        check(not obj(request('/api/super-mode'))['super_mode'],'super mode defaults off')
        check(request('/api/super-mode',{'enabled':True,'password':'1234'},agent=client())[0]==401,'anonymous super mode denied')
        check(request('/api/super-mode',{'enabled':True,'password':'wrong'})[0]==401 and not obj(request('/api/super-mode'))['super_mode'],'wrong administrator cannot enable super mode')
        time.sleep(2.1)
        obj(request('/api/super-mode',{'enabled':True,'password':'1234'}))
        visible=request('/api/entries');full=obj(visible)
        check(full['super_mode'] and all('password' in e for e in full['entries']) and visible[2]['Cache-Control']=='no-store','super session returns uncached plaintext records')
        check(not obj(request('/api/entries',agent=other))['super_mode'] and all('password' not in e for e in obj(request('/api/entries',agent=other))['entries']),'other login session stays masked')
        check(obj(request('/api/entries/'+identifier+'/reveal',{}))['password']=='Test-Only!4','super reveal needs no repeated administrator password')
        check(request('/api/entries/'+identifier+'/reveal',{},agent=other)[0]==400,'ordinary session cannot reuse another session privilege')
        visible=add(id=identifier,password='',note='changed')
        check(visible['entry']['password']=='Test-Only!4' and visible['super_mode'],'save response follows super mode')
        report=obj(request('/api/entries/import',dict(csv=text,category='web',preview=True)))
        check(report['super_mode'] and report['entries'][0]['password']=='Web-Test!4','super CSV preview shows matched credential')
        report=obj(request('/api/archive/import',dict(file=imported,preview=True)))
        check(report['super_mode'] and all(e['password']=='Test-Only!4' for e in report['entries']),'super archive preview shows plaintext')
        check(b'Test-Only!4' not in request('/api/archive/export',{})[1],'super export remains encrypted')
        obj(request('/api/super-mode',{'enabled':False}))
        check(not entries()['super_mode'] and all('password' not in e for e in entries()['entries']),'exit immediately removes server plaintext permission')
        check('password' not in obj(request('/api/entries/import',dict(csv=text,category='web',preview=True)))['entries'][0],'CSV masks again after exit')
        check('password' not in obj(request('/api/archive/import',dict(file=imported,preview=True)))['entries'][0],'archive masks again after exit')
        obj(request('/api/super-mode',{'enabled':True,'password':'1234'}));obj(request('/api/logout',{}))
        check(request('/api/super-mode')[0]==401,'lock invalidates privileged session')
        obj(request('/api/login',{'password':'1234'}))
        check(not entries()['super_mode'],'login after lock starts in normal mode')
        obj(request('/api/super-mode',{'enabled':True,'password':'1234'}))
        before_bundle=request('/api/device/bundle',headers=device_headers)[1]
        changed=request('/api/admin/password',dict(password='1234',new_password='四字新密',confirm='四字新密'));obj(changed)
        check(request('/api/entries',agent=other)[0]==401 and request('/api/entries')[0]==200,'password change revokes other sessions')
        check(not entries()['super_mode'],'administrator password change clears super mode')
        check(request('/api/device/bundle',headers=device_headers)[1]==before_bundle,'admin change leaves vault snapshot and keys unchanged')
        check(obj(request('/api/entries/'+identifier+'/reveal',{'password':'四字新密'}))['password']=='Test-Only!4','new password reveal')
        rev=entries()['revision'];payload=dict(title='concurrent',category='other',username='test',password='Concurrent!4',revision=rev)
        with ThreadPoolExecutor(max_workers=2) as pool:codes=list(pool.map(lambda _:request('/api/entries',payload)[0],range(2)))
        check(sorted(codes)==[200,409],'atomic concurrent revision conflict')
        obj(request('/api/super-mode',{'enabled':True,'password':'四字新密'}))
        snapshot=request('/api/device/bundle',headers=device_headers)[1];stop();start();browser=client()
        check(request('/api/entries')[0]==401,'restart clears sessions')
        obj(request('/api/login',{'password':'四字新密'}));check(request('/api/device/bundle',headers=device_headers)[1]==snapshot,'restart retains encrypted data and admin')
        check(not entries()['super_mode'],'restart does not persist super mode')
        for _ in range(100):assert request('/api/device/bundle',headers=device_headers)[1]==snapshot
        check(True,'100 repeat device downloads')
        current=decode(snapshot,keys);incoming=current+[dict(current[0],id=f'{i:032x}',username='capacity'+str(i)) for i in range(256-len(current))]
        rev=entries()['revision'];cap=base64.b64encode(encode(incoming,999,keys)).decode()
        committed=obj(request('/api/archive/import',dict(file=cap,preview=False,revision=rev)))
        check(committed['total']==256,'256 entries accepted')
        check(entries()['max_entries']==256 and len(decode(request('/api/device/bundle',headers=device_headers)[1],keys))==256,'256 entries in API and encrypted device snapshot')
        obj(request('/api/super-mode',{'enabled':True,'password':'四字新密'}))
        check(len(entries()['entries'])==256 and all('password' in e for e in entries()['entries']),'super mode handles full 256 records in one list read')
        obj(request('/api/super-mode',{'enabled':False}))
        check(request('/api/entries',dict(payload,revision=committed['revision']))[0]==409,'257th entry rejected')
        # Absolute body deadline: keep supplying data below socket timeout.
        connection=socket.create_connection(('127.0.0.1',port),3);connection.settimeout(5)
        connection.sendall((f'POST /api/login HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nOrigin: {origin}\r\nContent-Length: 100\r\nConnection: close\r\n\r\n').encode())
        began=time.monotonic()
        for _ in range(6):
            try:connection.sendall(b' ')
            except OSError:break
            time.sleep(2)
        raw=connection.recv(4096);connection.close()
        check(b'408' in raw.split(b'\r\n',1)[0] and time.monotonic()-began<16,'slow body absolute deadline')
        check(request('/healthz')[0]==200,'health after slow body timeout')
        stop();check(b'Test-Only!4' not in (work/'data/vault.db').read_bytes(),'no plaintext in SQLite')
        result={'ok':True,'passed':True,'checks':checks,'java':java,'jar_sha256':hashlib.sha256(Path(jar).read_bytes()).hexdigest(),'synthetic_only':True}
    except Exception as e:
        result={'ok':False,'passed':False,'checks':checks,'error':type(e).__name__+': '+str(e)}
        raise
    finally:
        try:stop()
        except Exception:
            if process is not None and process.poll() is None:process.terminate();process.wait(timeout=10)
        if 'result' in locals():(work/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'ok':True,'checks':len(checks),'work':str(work)}))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--java',required=True);p.add_argument('--jar',required=True);p.add_argument('--work',required=True);a=p.parse_args();run(a.java,a.jar,a.work)
