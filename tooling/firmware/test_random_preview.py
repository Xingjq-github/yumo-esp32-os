"""Isolated local Edge: random interactions, all themes and clock bounds."""
from pathlib import Path
import argparse,base64,json,subprocess,sys,time,traceback,os
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
import httpx,websocket

class CDP:
    def __init__(self,url):
        self.ws=websocket.create_connection(url,timeout=30,suppress_origin=True);self.ident=0;self.events=[]
    def call(self,method,params=None):
        self.ident+=1;self.ws.send(json.dumps({'id':self.ident,'method':method,'params':params or {}}))
        while True:
            data=json.loads(self.ws.recv())
            if data.get('id')==self.ident:
                if 'error' in data:raise RuntimeError(data['error'])
                return data.get('result',{})
            self.events.append(data)
    def evaluate(self,code):
        result=self.call('Runtime.evaluate',{'expression':code,'awaitPromise':True,'returnByValue':True})
        if 'exceptionDetails' in result:raise AssertionError(result['exceptionDetails'])
        return result['result'].get('value')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run-dir',type=Path,required=True);args=parser.parse_args()
    run=args.run_dir.resolve();assert run.is_relative_to((ROOT.parent/'artifacts/validation').resolve());run.mkdir(parents=True,exist_ok=False)
    browser=None;cdp=None;checks=[];captures=[];report={'passed':False}
    def check(ok,label):
        if not ok:raise AssertionError(label)
        checks.append(label)
    def wait(code,timeout=12):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if cdp.evaluate(code):return
            time.sleep(.06)
        raise AssertionError('Timeout: '+code)
    def ui(code):return cdp.evaluate("(()=>{const card=UnifiedPreview.cards.get('random'),s=card.controller.state,screen=card.surface.shadowRoot.querySelector('.random-screen');return "+code+';})()')
    def action(selector):ui('screen.querySelector('+json.dumps(selector)+').click()')
    def select(selector,value):ui('(()=>{const e=screen.querySelector('+json.dumps(selector)+');e.value='+json.dumps(str(value))+";e.dispatchEvent(new Event('change'));})()")
    def audit(label):
        result=ui("(()=>{const r=screen.getBoundingClientRect(),bad=[];for(const e of screen.querySelectorAll('button,select,input,.random-label,.random-result,.random-cell,.random-hint,.pk-result,.pk-score')){if(e.hidden||!e.getClientRects().length)continue;const b=e.getBoundingClientRect();if(b.left<r.left-.1||b.top<r.top-.1||b.right>r.right+.1||b.bottom>r.bottom+.1)bad.push([e.className,b.x-r.x,b.y-r.y,b.width,b.height]);if(/BUTTON|SELECT|INPUT/.test(e.tagName)&&(b.width<36||b.height<36))bad.push(['small target',e.className,b.width,b.height]);}return bad;})()")
        check(not result,label+': 240px bounds and touch targets '+str(result))
        check(ui("[...screen.querySelectorAll('.random-cell:not(.pk-cell)')].every(e=>e.offsetHeight>=26)"),label+': enough height for device glyph line')
    def capture(name):
        rect=ui("(()=>{const r=screen.getBoundingClientRect();return {x:r.x+scrollX,y:r.y+scrollY,width:240,height:240,scale:1};})()")
        data=cdp.call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':True,'clip':rect})
        path=run/(name+'.png');path.write_bytes(base64.b64decode(data['data']));captures.append(path)
    try:
        profile=run/'browser';profile.mkdir()
        with (run/'browser.log').open('wb') as log:
            browser=subprocess.Popen([str(Path(os.environ.get('ProgramFiles(x86)',os.environ['ProgramFiles']))/'Microsoft/Edge/Application/msedge.exe'),'--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--disable-extensions','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+str(profile),'about:blank'],stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        deadline=time.monotonic()+20
        while not (profile/'DevToolsActivePort').exists():
            if time.monotonic()>deadline or browser.poll() is not None:raise RuntimeError('Edge start failed')
            time.sleep(.1)
        port=int((profile/'DevToolsActivePort').read_text().splitlines()[0])
        with httpx.Client(trust_env=False) as client:target=next(t for t in client.get(f'http://127.0.0.1:{port}/json/list').json() if t['type']=='page')
        cdp=CDP(target['webSocketDebuggerUrl'])
        for domain in ('Page','Runtime','Network'):cdp.call(domain+'.enable')
        cdp.call('Emulation.setFocusEmulationEnabled',{'enabled':True})
        cdp.call('Emulation.setDeviceMetricsOverride',{'width':1360,'height':1000,'deviceScaleFactor':1,'mobile':False})
        cdp.call('Page.navigate',{'url':(ROOT/'../desktop/ui-preview/index.html').as_uri()})
        wait("document.body.dataset.ready==='true'")
        check(cdp.evaluate('UnifiedPreview.cards.size===7&&ClockPreview.themes.length===15'),'15 themes, seven independent scenes')
        cdp.evaluate("ClockPreview.setMode('frozen')")
        ids=cdp.evaluate('ClockPreview.themes.map(t=>t.id)')
        selected=['warm','mono','tiles','focus','terminal']
        for theme in ids:
            cdp.evaluate(f"UnifiedPreview.showTheme('{theme}')")
            wait("document.body.dataset.ready==='true'")
            cdp.evaluate("UnifiedPreview.cards.get('random').surface.scrollIntoView({block:'center'})")
            time.sleep(.15)
            check(cdp.evaluate('RandomPreview.stats.mounted===1&&ClockPreview.stats.mounted===3'),theme+': no stale controllers')
            for mode in range(8):
                select('.random-mode',mode);audit(theme+'/'+str(mode))
                if mode in (2,3):
                    for count in range(2,9 if mode==2 else 17):
                        select('.random-count',count);audit(theme+'/'+str(mode)+'/'+str(count))
                        check(ui('screen.querySelectorAll(".random-cell").length')==count,'exact option count')
                if theme in selected:
                    if mode==0:
                        ui("(()=>{s.low='-999999';s.high='-999999';screen.querySelectorAll('input').forEach(e=>e.value='-999999');})()")
                    action('.random-start');wait("!UnifiedPreview.cards.get('random').controller.state.running")
                    check(ui('s.step>0'),theme+'/'+str(mode)+': animation completed')
                    if mode==0:check(ui("screen.querySelector('.random-result').textContent==='-999999'"),'full signed number displayed')
                    if mode==4:
                        values=ui("[...screen.querySelectorAll('.random-ball')].map(e=>Number(e.textContent))")
                        check(len(set(values[:6]))==6 and values[:6]==sorted(values[:6]) and all(1<=x<=33 for x in values[:6]) and 1<=values[6]<=16,'sorted distinct lottery, valid blue')
                    if mode in (0,1,4):capture(theme+'-'+str(mode))
            action('.random-pk');audit(theme+'/pk')
            if theme in selected:
                ui("card.controller.key('boot')");wait("!UnifiedPreview.cards.get('random').controller.state.running")
                check(ui('s.players[0]===2&&s.players[1]===0&&s.result===-1'),'BOOT only blue; no early score')
                ui("card.controller.key('key')");wait("!UnifiedPreview.cards.get('random').controller.state.running")
                check(ui('s.players.every(p=>p===2)&&s.result>=0'),'KEY only red; resolves match')
                before=ui('s.score');time.sleep(.1);check(before==ui('s.score'),'score resolved once')
                capture(theme+'-pk')
                action('.random-start');check(ui('s.players.every(p=>p===1)'),'Start controls both sides')
                ui('card.controller.setVisible(false)');check(ui('!s.running&&s.result===-1'),'offscreen cancels unfinished match');ui('card.controller.setVisible(true)')
            for case in [{'year':2026,'month':12,'day':31,'hour':23,'minute':59,'second':58},{'year':2028,'month':2,'day':29,'hour':11,'minute':11,'second':11}]:
                cdp.evaluate('ClockPreview.setTime('+json.dumps(case)+')')
                check(cdp.evaluate("[...document.querySelectorAll('#clock-gallery yumo-clock')].every(c=>c.audit.texts.every(t=>t.x>=-.01&&t.y>=-.01&&t.x+t.w<=240.01&&t.y+t.h<=240.01))"),theme+': all three clock text bounds')
        cdp.evaluate("Promise.all(ClockPreview.themes.map(t=>UnifiedPreview.showTheme(t.id)))")
        wait("document.body.dataset.ready==='true'")
        check(cdp.evaluate('RandomPreview.stats.mounted===1&&RandomPreview.stats.timers===0'),'rapid theme switching releases timers')
        # New home routes (old simple random dialog must not reappear).
        for theme in ['warm','tiles','focus','terminal']:
            cdp.evaluate(f"UnifiedPreview.showTheme('{theme}')")
            cdp.evaluate("UnifiedPreview.showCard(UnifiedPreview.cards.get('random'),'home')")
            check(cdp.evaluate('RandomPreview.stats.mounted===0'),'leaving random releases controller')
            cdp.evaluate("UnifiedPreview.showCard(UnifiedPreview.cards.get('random'),'random')")
            check(cdp.evaluate('RandomPreview.stats.mounted===1'),'return to random creates one controller')
        # 360px mobile layout remains usable; no new horizontal page scroll.
        cdp.call('Emulation.setDeviceMetricsOverride',{'width':390,'height':844,'deviceScaleFactor':1,'mobile':True})
        check(cdp.evaluate('document.documentElement.scrollWidth<=innerWidth'),'mobile page width')
        errors=[e for e in cdp.events if e.get('method')=='Runtime.exceptionThrown']
        check(not errors,'no browser exceptions '+str(errors))
        requests=[e['params']['request']['url'] for e in cdp.events if e.get('method')=='Network.requestWillBeSent']
        check(not any(x.startswith(('http:','https:')) for x in requests),'no external network requests')
        try:
            from PIL import Image,ImageDraw
            sheet=Image.new('RGB',(5*260,4*270),'#eeeeea');draw=ImageDraw.Draw(sheet)
            for x,theme in enumerate(selected):
                for y,mode in enumerate(['0','1','4','pk']):
                    sheet.paste(Image.open(run/(theme+'-'+mode+'.png')),(x*260+10,y*270+25));draw.text((x*260+10,y*270+6),theme+' / '+mode,fill='black')
            sheet.save(run/'contact.png')
        except ImportError:pass
        report.update(passed=True,checks=len(checks),themes=len(ids),captures=len(captures))
    except Exception:
        report['error']=traceback.format_exc();print(report['error'])
    finally:
        if cdp:
            try:cdp.call('Browser.close')
            except Exception:pass
            cdp.ws.close()
        if browser:
            try:browser.wait(timeout=10)
            except subprocess.TimeoutExpired:browser.terminate();browser.wait(timeout=5)
        report['completed_checks']=checks;(run/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k!='completed_checks'},ensure_ascii=False))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
