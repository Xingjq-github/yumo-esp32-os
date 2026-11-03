"""Headless browser replay against loopback-only synthetic login/files endpoints."""
from pathlib import Path
import argparse, base64, http.server, json, os, subprocess, sys, threading, time, urllib.request

ROOT=Path(__file__).resolve().parents[2]
import websocket

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',required=True,type=Path)
    ap.add_argument('--browser',required=True,type=Path,help='External Chromium or Edge executable; websocket-client must be installed in Python')
    a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation')
    run.mkdir(parents=True,exist_ok=False)
    page=(ROOT/'firmware/main/core/lan_web.html').read_bytes()
    log=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def respond(self,status,data,content='application/json'):
            body=data if isinstance(data,bytes) else json.dumps(data).encode()
            self.send_response(status);self.send_header('Content-Type',content);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        def do_GET(self):
            if self.path=='/':self.respond(200,page,'text/html; charset=utf-8')
            elif self.path.startswith('/api/files'):self.respond(200,{'more':False,'entries':[]})
            elif self.path=='/api/passwords':self.respond(200,[])
            else:self.respond(404,{})
        def do_POST(self):
            body=self.rfile.read(int(self.headers.get('Content-Length','0'))).decode()
            if self.path=='/api/login':
                log.append(body)
                if body=='12369':self.respond(200,{})
                else:self.respond(401,'图形不正确'.encode(),'text/plain; charset=utf-8')
            else:self.respond(200,{})
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    edge=a.browser.resolve();assert edge.is_file()
    out=(run/'browser.log').open('wb')
    process=subprocess.Popen([str(edge),'--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check',
        '--disable-background-networking','--remote-debugging-port=0','--user-data-dir='+str(run/'profile'),'about:blank'],
        stdout=out,stderr=out,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    ws=None;count=0;checks=[]
    try:
        portfile=run/'profile/DevToolsActivePort'
        until=time.monotonic()+20
        while not portfile.exists() and time.monotonic()<until:time.sleep(.1)
        port=int(portfile.read_text().splitlines()[0])
        targets=json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json',timeout=5))
        target=next(x for x in targets if x['type']=='page')
        ws=websocket.create_connection(target['webSocketDebuggerUrl'],suppress_origin=True,timeout=10)
        def call(method,params=None):
            nonlocal count
            count+=1;own=count;ws.send(json.dumps({'id':own,'method':method,'params':params or {}}))
            while True:
                response=json.loads(ws.recv())
                if response.get('id')==own:
                    assert 'error' not in response,response
                    return response.get('result',{})
        def evaluate(expression):
            result=call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True})
            assert 'exceptionDetails' not in result,result
            return result.get('result',{}).get('value')
        def wait(expression):
            until=time.monotonic()+6
            while time.monotonic()<until:
                if evaluate(expression):return
                time.sleep(.04)
            raise AssertionError(expression)
        call('Emulation.setDeviceMetricsOverride',{'width':420,'height':880,'deviceScaleFactor':1,'mobile':True})
        call('Page.navigate',{'url':f'http://127.0.0.1:{server.server_port}/'})
        wait("document.readyState==='complete' && typeof patternDown !== 'undefined'")
        evaluate('tab(true)')
        assert evaluate("document.querySelectorAll('#patternDots circle').length") == 18
        assert not evaluate("!!document.querySelector('#pin')")
        checks.append('SVG grid only; no numeric password field')
        def stroke(points,release=True):
            box=evaluate("(()=>{const r=patternSvg.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}})()")
            for i,(x,y) in enumerate(points):
                call('Input.dispatchMouseEvent',{'type':'mousePressed' if i==0 else 'mouseMoved',
                    'x':box['x']+x*box['w']/240,'y':box['y']+y*box['h']/240,'button':'left','buttons':1,'clickCount':1 if i==0 else 0})
            if release:
                x,y=points[-1];call('Input.dispatchMouseEvent',{'type':'mouseReleased','x':box['x']+x*box['w']/240,'y':box['y']+y*box['h']/240,'button':'left','buttons':0,'clickCount':1})
        stroke([(48,48),(192,48)])
        assert not log;assert '4' in evaluate("$('patternHint').textContent")
        checks.append('Short stroke rejected without HTTP login')
        stroke([(48,48),(192,192),(48,192)])
        wait('!patternBusy')
        assert log==['15987'],log
        assert not evaluate("$('login').hidden")
        checks.append('Fast diagonal and reverse row insert exact crossed cells; failure remains locked')
        stroke([(48,48),(192,48),(192,192)])
        wait("$('login').hidden")
        assert log[-1]=='12369' and len(log)==2
        assert evaluate("document.querySelectorAll('.category-button').length")==4
        checks.append('Release submits once; successful pattern opens four categories')
        evaluate('lock();tab(true)')
        # Cancel while drawing: must never turn cancellation into a submission.
        stroke([(48,48),(192,48)],release=False)
        evaluate('resetPattern()')
        call('Input.dispatchMouseEvent',{'type':'mouseReleased','x':100,'y':200,'button':'left','buttons':0})
        assert len(log)==2 and evaluate('patternCells.length')==0
        checks.append('Cancelled pointer discards incomplete pattern')
        evaluate("patternHint('绘制设备图形密码')")
        screenshot=call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
        (run/'pattern-login.png').write_bytes(base64.b64decode(screenshot['data']))
        wait("$('patternHint').hidden")
        checks.append('Center hint disappears automatically')
        # JS and firmware share the same complete set of legal 4..9-cell paths.
        total=evaluate("(()=>{let count=0;function visit(path,used){if(path.length>=4)count++;if(path.length===9)return;for(let cell=0;cell<9;cell++){if(used&(1<<cell))continue;const middle=path.length?patternMiddle(path[path.length-1],cell):-1;if(middle>=0&&!(used&(1<<middle)))continue;visit([...path,cell],used|(1<<cell))}}visit([],0);return count})()")
        assert total==389112,total
        checks.append('389112 legal patterns match firmware model')
        (run/'result.json').write_text(json.dumps({'passed':True,'checks':checks,'synthetic_login_requests':len(log),'accepted_patterns':total},ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'passed':True,'checks':len(checks),'accepted_patterns':total}))
    finally:
        if ws:ws.close()
        process.terminate()
        try:process.wait(timeout=8)
        except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
        out.close();server.shutdown();server.server_close()

if __name__=='__main__':main()
