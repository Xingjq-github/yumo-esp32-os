"""Work groups, editable group suggestions and always-visible codes; synthetic only."""
import base64


def check_category_views(cdp, check, wait, run):
    def verify(expression, name):
        check(cdp.evaluate(expression), name)

    verify("""(async()=>{
      for(let i=0;i<42;i++){
        const r=await api('entries',{revision,category:'work',title:'Team '+i,target:'Office',username:'demo',password:'Synthetic-Work-'+i+'!',note:''});revision=r.revision;
      }
      for(const target of ['office','','','<b>Team</b>']){
        const r=await api('entries',{revision,category:'work',title:'工作示例',target,username:'other',password:'Synthetic-Work!',note:''});revision=r.revision;
      }
      await reload();category='work';selected=null;workName=null;render();
      return workGroups.length===5 && workByName.get('Office').entries.length===42 && workByName.get('').entries.length===2 && !document.querySelector('#content b');
    })()""", 'work groups preserve case empty names and every credential')
    cdp.evaluate("openWorkplace('Office')")
    verify("document.querySelectorAll('.entry').length===40 && !!document.querySelector('.next-page')", 'work group paginates at forty records')
    cdp.evaluate("document.querySelector('.next-page').click();globalThis.workRow=document.querySelector('.entry').dataset.entryId;document.querySelector('.entry').click();document.querySelector('#crumb button:last-of-type').click()")
    verify("document.querySelector('.entry').dataset.entryId===workRow", 'work detail returns to original page')
    cdp.evaluate("document.getElementById('add').click()")
    verify("document.getElementById('target').value==='Office' && document.getElementById('target').getAttribute('list')==='targetOptions' && [...document.querySelectorAll('#targetOptions option')].filter(o=>o.value==='Office').length===1 && ![...document.querySelectorAll('#targetOptions option')].some(o=>o.value==='Orbit')", 'work suggestions are deduplicated scoped and prefilled')
    cdp.evaluate("document.getElementById('target').value='New team';document.getElementById('title').value='手动新增系统';document.getElementById('username').value='new';document.getElementById('password').value='Synthetic-New!';document.getElementById('editForm').requestSubmit()")
    wait("!!selected && selected.target==='New team' && document.getElementById('editor').hidden")
    verify("workName==='New team' && workByName.get('New team').entries.length===1", 'manual system name creates a new group after save')
    cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('target').value='Office';document.getElementById('editForm').requestSubmit()")
    wait("!!selected && selected.target==='Office' && document.getElementById('editor').hidden")
    verify("!workByName.has('New team') && workByName.get('Office').entries.length===43", 'existing system selection moves entry without merging passwords')
    cdp.evaluate("openApplication('Orbit');document.getElementById('add').click()")
    verify("[...document.querySelectorAll('#targetOptions option')].some(o=>o.value==='Orbit') && ![...document.querySelectorAll('#targetOptions option')].some(o=>o.value==='Office') && document.getElementById('target').value==='Orbit'", 'application suggestions never mix work systems')
    cdp.evaluate("document.getElementById('category').value='web';document.getElementById('category').dispatchEvent(new Event('change'))")
    verify("!document.getElementById('target').hasAttribute('list') && !document.getElementById('targetOptions').children.length", 'non-group categories clear suggestions')
    cdp.evaluate("document.getElementById('editCancel').click();openEntry(items.find(e=>e.category==='other'))")
    verify("!document.querySelector('[data-field=username]') && !document.getElementById('showSecret') && document.getElementById('secret').textContent==='Synthetic-View!4'", 'other detail has code without account or reveal control')
    cdp.evaluate("hideSecret();document.getElementById('refresh').click()")
    wait("document.getElementById('secret')?.textContent==='Synthetic-View!4'")
    verify("!secretTimer || document.getElementById('secret').textContent==='Synthetic-View!4'", 'normal hide and refresh keep code visible')
    cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click()")
    verify("document.getElementById('username').disabled && document.getElementById('username').closest('label').hidden && document.getElementById('password').type==='text' && document.getElementById('password').value==='Synthetic-View!4'", 'code editor shows saved code and excludes account')
    cdp.evaluate("document.getElementById('password').value='Updated-Code!';document.getElementById('editForm').requestSubmit()")
    wait("!!selected && selected.password==='Updated-Code!' && document.getElementById('editor').hidden")
    cdp.evaluate("document.querySelectorAll('#crumb button')[1].click()")
    verify("document.querySelector('.code-value').textContent==='码值：Updated-Code!' && !document.querySelector('.plain-password')", 'other list displays updated code in ordinary mode')
    cdp.evaluate("document.getElementById('superMode').click();document.getElementById('superAdmin').value='1234';document.getElementById('superForm').requestSubmit()")
    wait("superMode && !superBusy")
    cdp.evaluate("document.querySelector('.entry').click();document.getElementById('superMode').click()")
    wait("!superMode && !superBusy")
    verify("document.getElementById('secret').textContent==='Updated-Code!' && items.filter(e=>e.category!=='other').every(e=>!('password' in e))", 'super exit preserves code but clears all ordinary credentials')
    # Switch a code into a protected category through a real save, then back.
    cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('category').value='app';document.getElementById('category').dispatchEvent(new Event('change'))")
    verify("document.getElementById('password').type==='password' && !document.getElementById('username').disabled", 'switching from other restores password masking and account')
    cdp.evaluate("document.getElementById('editForm').requestSubmit()")
    wait("selected?.category==='app' && document.getElementById('editor').hidden")
    verify("!('password' in selected) && document.getElementById('secret').textContent==='••••••••'", 'saved protected category no longer sends plaintext')
    cdp.evaluate("[...document.querySelectorAll('#content button')].find(e=>e.textContent==='编辑').click();document.getElementById('category').value='other';document.getElementById('category').dispatchEvent(new Event('change'));document.getElementById('password').value='Restored-Code!';document.getElementById('editForm').requestSubmit()")
    wait("selected?.category==='other' && document.getElementById('editor').hidden")
    for width,height,size in [(1080,900,'desktop'),(390,844,'mobile')]:
        cdp.call('Emulation.setDeviceMetricsOverride',dict(width=width,height=height,deviceScaleFactor=1,mobile=width<500))
        verify("document.documentElement.scrollWidth<=window.innerWidth", 'code detail fits '+size)
        shot=cdp.call('Page.captureScreenshot',{'format':'png'})
        (run/('code-detail-'+size+'.png')).write_bytes(base64.b64decode(shot['data']))
    # Lock while the code and group suggestions have been used; nothing survives.
    cdp.evaluate("document.getElementById('logout').click()")
    wait("!document.getElementById('gate').hidden")
    verify("!document.getElementById('content').textContent && !document.getElementById('password').value && !items.length && !workByName.size && !document.getElementById('targetOptions').children.length", 'lock clears code and system names')
    cdp.evaluate("document.getElementById('admin').value='1234';document.getElementById('loginForm').requestSubmit()")
    wait("!document.getElementById('vault').hidden")
