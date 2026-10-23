"""Entry fields and application navigation against an isolated synthetic vault."""
import base64
import json


def check_entry_views(cdp, check, wait, run):
    def verify(expression, name):
        check(cdp.evaluate(expression), name)

    verify("""(async()=>{
      for(const category of ['work','app','web','other']){
        const r=await api('entries',{revision,category,title:'示例记录 '+category,target:'示例平台',username:'demo',password:'Synthetic-View!4',note:'示例备注'});revision=r.revision;
      }
      for(let i=0;i<42;i++){
        const r=await api('entries',{revision,category:'app',title:'Orbit '+i,target:'Orbit',username:'demo',password:'Synthetic-'+i+'!',note:''});revision=r.revision;
      }
      for(const target of ['','','未填写应用名称','orbit','<img src=x onerror=alert(1)>']){
        const r=await api('entries',{revision,category:'app',title:'边界示例',target,username:'edge',password:'Synthetic-Edge!4',note:''});revision=r.revision;
      }
      await reload();return items.length===51;
    })()""", 'entry views synthetic fixtures created')
    for kind, label in [('work', '单位 / 系统'), ('app', '应用名称 / 平台'), ('web', '网页地址'), ('other', '用途 / 位置')]:
        cdp.evaluate("openEntry(items.find(e=>e.title==='示例记录 '+"+json.dumps(kind)+"))")
        expected = json.dumps(['分类', '名称', label, *([] if kind=='other' else ['账号']), '码值' if kind=='other' else '密码', '备注'], ensure_ascii=False, separators=(',', ':'))
        verify("JSON.stringify([...document.querySelectorAll('.detail-field>small')].map(e=>e.textContent))==="+json.dumps(expected, ensure_ascii=False), 'detail complete fields '+kind)
        verify("document.querySelector('[data-field=title] .value').textContent===selected.title && document.querySelector('[data-field=target] .value').textContent===selected.target && document.getElementById('secret').textContent==="+json.dumps('Synthetic-View!4' if kind=='other' else '••••••••'), 'detail category visibility '+kind)
        cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click()")
        verify("JSON.stringify([...document.querySelectorAll('#editor [data-field-label]')].filter(e=>!e.closest('label').hidden).map(e=>e.textContent))==="+json.dumps(expected, ensure_ascii=False)+" && document.getElementById('password').value==="+json.dumps('Synthetic-View!4' if kind=='other' else ''), 'editor matches detail labels '+kind)
        cdp.evaluate("document.getElementById('editCancel').click()")
    from browser_category_views import check_category_views
    check_category_views(cdp, check, wait, run)
    cdp.evaluate("openApplication('Orbit')")
    verify("document.querySelectorAll('.entry').length===40 && appByName.get('Orbit').entries.length===42", 'app pagination preserves same-account distinct credentials')
    cdp.evaluate("document.querySelector('.next-page').click();window.scrollTo(0,80);globalThis.viewY=window.scrollY;globalThis.viewId=document.querySelector('.entry').dataset.entryId;document.querySelector('.entry').click();document.querySelector('#crumb button:last-of-type').click()")
    verify("document.querySelector('.entry').dataset.entryId===viewId && Math.abs(window.scrollY-viewY)<2", 'app detail return restores page and scroll')
    cdp.evaluate("document.querySelectorAll('#crumb button')[1].click()")
    verify("document.querySelectorAll('.entry.application').length===6 && appByName.get('').entries.length===2 && appByName.get('未填写应用名称').entries.length===1 && appByName.get('orbit').entries.length===1 && !document.querySelector('#content img')", 'application names group exactly and render as text')
    cdp.evaluate("document.getElementById('search').value='Orbit 41';document.getElementById('search').dispatchEvent(new Event('input'));document.querySelector('.application').click()")
    verify("document.querySelectorAll('.entry').length===1 && document.querySelector('.entry strong').textContent==='Orbit 41'", 'application search finds child record name')
    cdp.evaluate("document.getElementById('search').value='';openApplication('orbit')")
    verify("document.querySelectorAll('.entry').length===1 && !document.querySelector('.list-pages')", 'application pagination state isolated by exact name')
    cdp.evaluate("document.getElementById('add').click()")
    verify("document.getElementById('target').value==='orbit' && document.getElementById('category').value==='app'", 'new application entry prefills group name')
    cdp.evaluate("document.getElementById('editCancel').click();document.querySelector('.entry').click();[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('target').value='Renamed';document.getElementById('editForm').requestSubmit()")
    wait("!!selected && selected.target==='Renamed' && document.getElementById('editor').hidden")
    verify("appName==='Renamed' && !appByName.has('orbit') && document.getElementById('crumb').textContent.includes('Renamed')", 'renaming application follows moved record')
    verify("(async()=>{const r=await api('entries/'+selected.id+'/reveal',{password:'1234'});return r.password==='Synthetic-Edge!4';})()", 'blank password edit preserves stored password')
    cdp.evaluate("globalThis.originalConfirm=window.confirm;window.confirm=()=>true;document.querySelector('#content .danger').click()")
    wait("selected===null && appName===null && !appByName.has('Renamed')")
    cdp.evaluate("window.confirm=originalConfirm")
    verify("!!document.querySelector('.application')", 'delete last entry returns to application groups')
    cdp.evaluate("document.getElementById('superMode').click();document.getElementById('superAdmin').value='1234';document.getElementById('superForm').requestSubmit()")
    wait("superMode && !superBusy")
    cdp.evaluate("openApplication('Orbit')")
    verify("document.querySelectorAll('.plain-password').length>0", 'super mode shows application credentials')
    cdp.evaluate("document.getElementById('superMode').click()")
    wait("!superMode && !superBusy")
    verify("!document.querySelector('.plain-password') && appByName.get('Orbit').entries.every(e=>!('password' in e))", 'exiting super mode clears application plaintext')
    for width, height, size in [(1080, 900, 'desktop'), (390, 844, 'mobile')]:
        cdp.call('Emulation.setDeviceMetricsOverride', dict(width=width, height=height, deviceScaleFactor=1, mobile=width<500))
        for view, action in [('groups', "selected=null;category='app';appName=null;render()"), ('detail', "openEntry(items.find(e=>e.title==='示例记录 app'))"), ('editor', "edit(selected)")]:
            cdp.evaluate(action)
            verify("document.documentElement.scrollWidth<=window.innerWidth", 'entry view fits '+view+' '+size)
            shot = cdp.call('Page.captureScreenshot', {'format': 'png'})
            (run/('entry-'+view+'-'+size+'.png')).write_bytes(base64.b64decode(shot['data']))
    cdp.evaluate("document.getElementById('editCancel').click()")
    verify("""(async()=>{
      for(const entry of [...items]){const r=await api('entries/'+entry.id+'/delete',{revision});revision=r.revision;}
      selected=null;category=null;appName=null;webAccount=null;document.getElementById('search').value='';listViews.clear();await reload();return items.length===0;
    })()""", 'entry view fixtures removed before existing regression suite')
