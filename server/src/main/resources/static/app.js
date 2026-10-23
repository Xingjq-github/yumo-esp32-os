'use strict';
const $=id=>document.getElementById(id), names={work:'工作',app:'应用',web:'网页',other:'其他'};
const TARGET_FIELDS={work:{label:'单位 / 系统',placeholder:'例如：公司邮箱系统（可选）'},app:{label:'应用名称 / 平台',placeholder:'例如：微信、GitHub（可选）'},web:{label:'网页地址',placeholder:'https://example.com'},other:{label:'用途 / 位置',placeholder:'可选'}};
function isCode(item){return item?.category==='other';}
function entryFields(type){return [['category','分类'],['title','名称'],['target',TARGET_FIELDS[type].label],...(type==='other'?[]:[['username','账号']]),['password',type==='other'?'码值':'密码'],['note','备注']];}
let initialized=false,items=[],maxEntries=256,revision=0,category=null,selected=null,editing=null,secretTimer=null,epoch=0,lastActivity=Date.now(),sessionEpoch=0;
let webAccount=null,webGroups=[],webByAccount=new Map(),byCategory=new Map(),searchById=new Map(),ordinalById=new Map();
let appName=null,appGroups=[],appByName=new Map();
let workName=null,workGroups=[],workByName=new Map();
const LIST_PAGE_SIZE=40,listViews=new Map();
let renderedListKey=null,renderedListPage=0;
let superMode=false,superBusy=false;
const modeChannel=typeof BroadcastChannel==='function'?new BroadcastChannel('joyvault-session'):null;
function setSuperBusy(busy){
  superBusy=busy;
  for(const id of ['superMode','superSubmit','superCancel'])$(id).disabled=busy;
}
function clearSuperDialog(){
  $('superAdmin').value='';$('superError').textContent='';$('superDialog').close();setSuperBusy(false);
}
function setSuperMode(enabled){
  superMode=enabled;
  if(!enabled){
    if(editing&&!isCode(editing)&&$('password').value===editing.password)$('password').value='';
    for(const item of items)if(!isCode(item))delete item.password;
    if(selected&&!isCode(selected))delete selected.password;if(editing&&!isCode(editing))delete editing.password;
    document.querySelectorAll('.plain-password').forEach(e=>e.remove());
  }
  $('password').type=enabled||$('category').value==='other'?'text':'password';
  $('superMode').textContent=enabled?'退出超管':'超管模式';
  $('superMode').classList.toggle('primary',enabled);
  $('superMode').setAttribute('aria-pressed',String(enabled));
  $('superStatus').hidden=!enabled;
  hideSecret();
}
function refreshModeDisplay(){
  if(!$('editor').hidden){
    if(superMode&&editing&&$('password').value==='')$('password').value=editing.password||'';
  }else if(!$('importer').hidden){
    if(superMode&&!$('importPreview').hidden)$('importForm').requestSubmit();
  }else if(!$('archivePanel').hidden){
    if(superMode&&!$('archiveResult').hidden)$('archiveForm').requestSubmit();
  }else if(!$('vault').hidden)render();
}
function receiveEntries(data){
  // Only explicit verification can enter the mode. A delayed response must not
  // turn it back on after exit, even if that response still carries plaintext.
  setSuperMode(superMode&&data.super_mode===true);
  if(!superMode)for(const item of data.entries)if(!isCode(item))delete item.password;
  items=data.entries;rebuildBrowseIndex();maxEntries=data.max_entries;revision=data.revision;
  $('summary').textContent=`${items.length} 条密码 · 版本 ${revision}`;
  if(selected){selected=items.find(e=>e.id===selected.id)||null;if(selected)category=selected.category;}
  if(editing)editing=items.find(e=>e.id===editing.id)||null;
}
async function exitSuperMode(){
  sessionEpoch++;setSuperMode(false);refreshModeDisplay();setSuperBusy(true);
  modeChannel?.postMessage('normal');
  try{await api('super-mode',{enabled:false});notice('已退出超管模式');}
  catch(error){if(!error.stale){lockVault();notice('模式退出未确认，已锁定，请重新登录');}}
  finally{setSuperBusy(false);}
}
function lockVault(){
  gate();modeChannel?.postMessage('lock');
  return api('logout',{}).catch(error=>{if(!error.stale)notice('页面已锁定，连接恢复后请重新登录');});
}
$('superMode').onclick=()=>{
  if(superBusy)return;
  if(superMode){exitSuperMode();return;}
  hideSecret();clearSuperDialog();$('superDialog').showModal();$('superAdmin').focus();
};
$('superCancel').onclick=clearSuperDialog;
$('superDialog').addEventListener('cancel',event=>{if(superBusy)event.preventDefault();});
$('superDialog').addEventListener('close',()=>{$('superAdmin').value='';});
$('superForm').onsubmit=async event=>{
  event.preventDefault();if(superBusy)return;
  const version=++sessionEpoch,body={enabled:true,password:$('superAdmin').value};let elevated=false;
  $('superAdmin').value='';$('superError').textContent='';setSuperBusy(true);
  try{
    await api('super-mode',body);elevated=true;body.password='';setSuperMode(true);
    const data=await api('entries');
    receiveEntries(data);clearSuperDialog();refreshModeDisplay();notice('超管模式已开启，密码明文展示');
  }catch(error){
    if(!error.stale&&version===sessionEpoch){
      if(elevated){
        try{await api('super-mode',{enabled:false});setSuperMode(false);}
        catch(revokeError){if(!revokeError.stale)lockVault();}
      }
      if(version===sessionEpoch)$('superError').textContent=error.message;
    }
  }finally{body.password='';if(version===sessionEpoch)setSuperBusy(false);}
};
if(modeChannel)modeChannel.onmessage=event=>{
  if(event.data==='lock')gate();
  else if(event.data==='normal'){sessionEpoch++;clearSuperDialog();setSuperMode(false);refreshModeDisplay();}
};
// Reconcile revocation while a tab was suspended, without background polling.
document.addEventListener('visibilitychange',()=>{
  if(document.hidden||$('gate').hidden===false)return;
  const version=sessionEpoch;
  api('super-mode').then(data=>{
    if(!data.super_mode&&superMode){sessionEpoch++;setSuperMode(false);refreshModeDisplay();}
  }).catch(error=>{if(!error.stale&&version===sessionEpoch&&error.status===401)gate();});
});
window.addEventListener('pagehide',()=>{
  gate();modeChannel?.postMessage('lock');
  fetch('/api/logout',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:'{}',keepalive:true}).catch(()=>{});
});
function accountName(value){return value||'未填写账号';}
function applicationName(value){return value||'未填写应用名称';}
function workplaceName(value){return value||'未填写单位 / 系统';}
function purposeName(item){return item.target||item.title||'未填写用途';}
function compareAccounts(a,b){const x=a.toLowerCase(),y=b.toLowerCase();return x<y?-1:x>y?1:a<b?-1:a>b?1:0;}
function rebuildBrowseIndex(){
  webByAccount=new Map();byCategory=new Map(Object.keys(names).map(key=>[key,[]]));searchById=new Map();ordinalById=new Map();
  appByName=new Map();workByName=new Map();
  for(const item of items){
    byCategory.get(item.category)?.push(item);
    searchById.set(item.id,[item.title,isCode(item)?'':item.username,item.target].join(' ').toLowerCase());
    if(item.category==='web'){
      if(!webByAccount.has(item.username))webByAccount.set(item.username,{account:item.username,entries:[]});
      const group=webByAccount.get(item.username);group.entries.push(item);ordinalById.set(item.id,group.entries.length);
    }
    if(item.category==='work'){
      const name=item.target||'';
      if(!workByName.has(name))workByName.set(name,{name,entries:[]});
      workByName.get(name).entries.push(item);
    }
    if(item.category==='app'){
      const name=item.target||'';
      if(!appByName.has(name))appByName.set(name,{name,entries:[]});
      appByName.get(name).entries.push(item);
    }
  }
  webGroups=[...webByAccount.values()].sort((a,b)=>compareAccounts(a.account,b.account));
  appGroups=[...appByName.values()].sort((a,b)=>compareAccounts(a.name,b.name));
  workGroups=[...workByName.values()].sort((a,b)=>compareAccounts(a.name,b.name));
}
function rememberList(){
  if(renderedListKey!==null){
    listViews.set(renderedListKey,{page:renderedListPage,y:window.scrollY});
    if(listViews.size>64)listViews.delete(listViews.keys().next().value);
  }
  renderedListKey=null;
}
function openAccount(account){category='web';webAccount=account;appName=null;selected=null;render();}
function openWorkplace(name){category='work';workName=name;appName=webAccount=null;selected=null;render();}
function openApplication(name){category='app';appName=name;webAccount=null;selected=null;render();}
function openEntry(item){selected=item;category=item.category;webAccount=item.category==='web'?item.username:null;appName=item.category==='app'?(item.target||''):null;workName=item.category==='work'?(item.target||''):null;render();}
function entryRow(title,subtitle,fn,cls,password){
  const b=button('',fn,'entry '+cls),text=node('span',undefined,'text');
  text.append(node('strong',title),node('small',subtitle));if(superMode&&typeof password==='string')text.append(node('span',password,'plain-password'));b.append(text,node('span','›'));b.title=title;return b;
}
function renderRows(rows,makeRow,q){
  const content=$('content'),key=JSON.stringify([category,category==='work'?workName:category==='app'?appName:webAccount,q]);
  const saved=listViews.get(key)||{page:0,y:0},pages=Math.max(1,Math.ceil(rows.length/LIST_PAGE_SIZE));
  const page=Math.min(saved.page,pages-1),fragment=document.createDocumentFragment();
  for(const row of rows.slice(page*LIST_PAGE_SIZE,(page+1)*LIST_PAGE_SIZE))fragment.append(makeRow(row));
  if(!rows.length)fragment.append(node('p','此分类暂无匹配的密码。'));
  if(pages>1){
    const pager=node('div',undefined,'list-pages');
    const move=next=>{rememberList();listViews.set(key,{page:next,y:0});render();};
    const prev=button('上一页',()=>move(page-1),'prev-page'),next=button('下一页',()=>move(page+1),'next-page');
    prev.disabled=page===0;next.disabled=page===pages-1;
    pager.append(prev,node('span',`${page+1} / ${pages} · ${rows.length} 项`),next);fragment.append(pager);
  }
  content.append(fragment);renderedListKey=key;renderedListPage=page;window.scrollTo(0,saved.y);
}
function node(tag,text,cls){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e;}
function button(text,fn,cls){const e=node('button',text,cls);e.type='button';e.onclick=fn;return e;}
function notice(text=''){$('notice').textContent=text;}
async function api(path,body){const expected=sessionEpoch;const r=await fetch('/api/'+path,{method:body===undefined?'GET':'POST',credentials:'same-origin',headers:body===undefined?{}:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),cache:'no-store'});const data=await r.json();if(expected!==sessionEpoch){const e=new Error('会话已改变');e.stale=true;throw e;}if(!r.ok){const e=new Error(data.detail||'请求失败');e.status=r.status;throw e;}return data;}
function hideSecret(){epoch++;clearTimeout(secretTimer);$('revealAdmin').value='';$('revealDialog').close();const value=$('secret');if(value)value.textContent=selected&&(superMode||isCode(selected))?(selected.password||''):'••••••••';const toggle=$('showSecret');if(toggle)toggle.textContent='展示密码';}
function gate(){sessionEpoch++;clearSuperDialog();setSuperMode(false);$('superMode').hidden=true;clearArchive();$('archivePanel').hidden=true;clearChange();$('changeAdmin').hidden=true;hideSecret();clearImport();$('importer').hidden=true;items=[];rebuildBrowseIndex();webAccount=appName=workName=null;listViews.clear();renderedListKey=null;selected=null;editing=null;category=null;$('content').replaceChildren();$('editForm').reset();$('targetOptions').replaceChildren();$('gate').hidden=false;$('vault').hidden=true;$('editor').hidden=true;$('logout').hidden=true;$('admin').value='';$('confirm').value='';$('confirmLabel').hidden=initialized;$('confirm').required=!initialized;$('gateTitle').textContent=initialized?'管理员登录':'首次使用 · 设置管理员密码';$('loginButton').textContent=initialized?'进入':'设置并进入';}
async function reload(){receiveEntries(await api('entries'));render();}
async function attempt(fn){try{await fn();}catch(e){if(e.stale)return;notice(e.message);if(e.status===401)gate();}}
function renderDetail(content){
  const detail=node('div',undefined,'detail');
  for(const [key,label] of entryFields(selected.category)){
    const field=node('div',undefined,'detail-field');field.dataset.field=key;
    const heading=node('small',label);heading.id='detail-'+key+'-label';
    const value=node('div',key==='password'?((superMode||isCode(selected))?(selected.password||''):'••••••••'):key==='category'?names[selected.category]:(selected[key]||'未填写'),'value');
    value.setAttribute('aria-labelledby',heading.id);field.append(heading,value);
    if(key==='password'){
      value.id='secret';
      const show=button('展示密码',()=>{if(value.textContent!=='••••••••'){hideSecret();return;}epoch++;$('revealError').textContent='';$('revealDialog').showModal();$('revealAdmin').focus();});
      show.id='showSecret';show.hidden=superMode||isCode(selected);if(!isCode(selected))field.append(show);
    }
    detail.append(field);
  }
  const actions=node('div',undefined,'actions');
  actions.append(button('编辑',()=>edit(selected)),button('删除',()=>attempt(async()=>{if(!confirm('删除“'+selected.title+'”？'))return;await api('entries/'+selected.id+'/delete',{revision});selected=null;await reload();}),'danger'));
  content.append(detail,actions);window.scrollTo(0,0);
}
function render(){rememberList();clearArchive();$('archivePanel').hidden=true;$('changeAdmin').hidden=false;hideSecret();clearImport();$('importer').hidden=true;$('gate').hidden=true;$('editor').hidden=true;$('vault').hidden=false;$('logout').hidden=false;$('superMode').hidden=false;
if(category!=='web')webAccount=null;
else if(selected)webAccount=selected.username;
else if(webAccount!==null&&!webByAccount.has(webAccount))webAccount=null;
if(category!=='work')workName=null;
else if(selected)workName=selected.target||'';
else if(workName!==null&&!workByName.has(workName))workName=null;
if(category!=='app')appName=null;
else if(selected)appName=selected.target||'';
else if(appName!==null&&!appByName.has(appName))appName=null;
const crumb=$('crumb');crumb.replaceChildren(button('全部分类',()=>{category=null;webAccount=appName=workName=null;selected=null;$('search').value='';render();}));
if(category)crumb.append(node('span','›'),button(names[category],()=>{selected=null;webAccount=appName=workName=null;render();}));
if(category==='web'&&webAccount!==null)crumb.append(node('span','›'),button(accountName(webAccount),()=>{selected=null;render();}));
if(category==='work'&&workName!==null)crumb.append(node('span','›'),button(workplaceName(workName),()=>{selected=null;render();}));
if(category==='app'&&appName!==null)crumb.append(node('span','›'),button(applicationName(appName),()=>{selected=null;render();}));
if(selected)crumb.append(node('span','›'),node('span',category==='web'?purposeName(selected):selected.title));const content=$('content');content.replaceChildren();$('search').hidden=!!selected;$('add').hidden=!!selected;$('importOpen').hidden=!!selected||category!=='web';
if(selected){renderDetail(content);return;}
$('search').placeholder=category==='work'?'搜索单位、系统、名称或账号':category==='app'?'搜索应用名称、名称或账号':category==='other'?'搜索名称或用途':'搜索名称、账号或用途';
const q=$('search').value.trim().toLowerCase();if(!category&&!q){const grid=node('div',undefined,'categories');for(const key of Object.keys(names)){const b=button('',()=>{category=key;webAccount=appName=workName=null;render();},'category');b.append(node('strong',names[key]),node('small',(byCategory.get(key)?.length||0)+' 条 ›'));grid.append(b);}content.append(grid);if(!items.length)content.append(node('p','还没有密码，点击“新建”开始。'));window.scrollTo(0,0);return;}
const matches=item=>searchById.get(item.id).includes(q);
if(category==='work'&&workName===null){
  const groups=workGroups.filter(group=>!q||group.entries.some(matches));
  renderRows(groups,group=>{const b=entryRow(workplaceName(group.name),`${group.entries.length} 条密码`,()=>openWorkplace(group.name),'workplace');b.dataset.workplace=group.name;return b;},q);return;
}
if(category==='app'&&appName===null){
  const groups=appGroups.filter(group=>!q||group.entries.some(matches));
  renderRows(groups,group=>{const b=entryRow(applicationName(group.name),`${group.entries.length} 条密码`,()=>openApplication(group.name),'application');b.dataset.application=group.name;return b;},q);return;
}
if(category==='web'&&webAccount===null){
  const groups=webGroups.filter(group=>!q||group.entries.some(matches));
  renderRows(groups,group=>{const b=entryRow(accountName(group.account),`${group.entries.length} 条用途`,()=>openAccount(group.account),'account');b.dataset.account=group.account;return b;},q);return;
}
const source=category==='work'?(workByName.get(workName)?.entries||[]):category==='app'?(appByName.get(appName)?.entries||[]):category==='web'?(webByAccount.get(webAccount)?.entries||[]):category?byCategory.get(category):items;
renderRows(source.filter(matches),item=>{
  const purpose=category==='web',subtitle=purpose?(item.title!==item.username&&item.title!==item.target?item.title:'记录 '+ordinalById.get(item.id)):(category==='app'||category==='work')?'账号：'+accountName(item.username):isCode(item)?(item.target||'码值'):(item.username||item.target||'无账号')+(!category?' · '+names[item.category]+(item.category==='app'?' · '+applicationName(item.target):''):'');
  const b=entryRow(purpose?purposeName(item):item.title,subtitle,()=>openEntry(item),purpose?'purpose':'',isCode(item)?undefined:item.password);if(isCode(item))b.querySelector('.text').append(node('span','码值：'+(item.password||''),'code-value'));b.dataset.entryId=item.id;return b;
},q);}
function targetLabel(){
  const type=$('category').value,code=type==='other';
  for(const [key,label] of entryFields(type))document.querySelector(`[data-field-label="${key}"]`).textContent=label;
  $('username').closest('label').hidden=code;
  $('username').disabled=code;
  $('password').type=code||superMode?'text':'password';
  // Do not carry a known password into a newly selected plaintext category.
  if(editing&&!isCode(editing)&&code&&$('password').value===editing.password)$('password').value='';
  if(editing&&isCode(editing)&&code&&$('password').value==='')$('password').value=editing.password||'';
  const valueName=code?'码值':'密码';
  $('editTitle').textContent=(editing?'编辑':'新建')+valueName;
  $('keepHint').textContent=code?'码值直接展示；保存后可在设备同步。':editing?'密码留空表示保留原密码；保存后即可同步。':'保存后即可在设备点击同步。';
  $('password').required=code||!editing;
  $('generatePassword').textContent=code?'生成码值':'填入强密码';
  document.querySelector('.gen-title').textContent=code?'码值生成选项':'强密码选项';
  $('target').placeholder=TARGET_FIELDS[type].placeholder;
  const options=$('targetOptions');options.replaceChildren();
  const groups=type==='work'?workGroups:type==='app'?appGroups:[];
  for(const group of groups)if(group.name){const option=node('option');option.value=group.name;options.append(option);}
  if(type==='work'||type==='app'){$('target').setAttribute('list','targetOptions');$('target').placeholder='选择已有名称，或输入新名称';}
  else $('target').removeAttribute('list');
}
function randomBelow(max){
  // getRandomValues also works on LAN HTTP. Rejection sampling avoids modulo
  // bias; no Math.random fallback and no password sent to a separate service.
  if(!(globalThis.crypto&&crypto.getRandomValues))throw new Error('随机数不可用');
  const limit=256-256%max,bytes=new Uint8Array(1);
  try{
    for(let attempt=0;attempt<256;attempt++){
      crypto.getRandomValues(bytes);
      if(bytes[0]<limit)return bytes[0]%max;
    }
    throw new Error('随机数不可用');
  }finally{bytes.fill(0);}
}
const PASSWORD_SETS={upper:'ABCDEFGHIJKLMNOPQRSTUVWXYZ',lower:'abcdefghijklmnopqrstuvwxyz',digit:'0123456789',symbol:'!#$%&()*+,-./:;<=>?@[]^_{|}~'};
function recommendedPassword(length,sets){
  // Build one char from each selected set, fill the rest from the union, then
  // shuffle, so every selected type is guaranteed present. Charset unchanged.
  const groups=sets.map(key=>PASSWORD_SETS[key]).filter(Boolean);
  if(!groups.length)throw new Error('no-set');
  if(length<groups.length)throw new Error('too-short');
  const alphabet=groups.join(''),out=[];
  for(const group of groups)out.push(group[randomBelow(group.length)]);
  for(let i=out.length;i<length;i++)out.push(alphabet[randomBelow(alphabet.length)]);
  for(let i=out.length-1;i>0;i--){const j=randomBelow(i+1),tmp=out[i];out[i]=out[j];out[j]=tmp;}
  return out.join('');
}
function generatorLength(){
  const raw=parseInt($('genLength').value,10),value=Number.isFinite(raw)?raw:16;
  return Math.min(64,Math.max(4,value));
}
$('generatePassword').onclick=()=>{
  const length=generatorLength(),sets=[];
  for(const [id,key] of [['genUpper','upper'],['genLower','lower'],['genDigit','digit'],['genSymbol','symbol']])if($(id).checked)sets.push(key);
  $('genLength').value=length;
  if(!sets.length){notice('请至少勾选一种字符类型：大写、小写、数字或符号。');return;}
  try{
    $('password').value=recommendedPassword(length,sets);
    $('keepHint').textContent='已填入'+length+'位推荐密码，保存后生效。';
    notice();
  }catch(error){notice(error.message==='too-short'?'位数不能小于所选字符类型的数量。':'无法生成强密码，请检查浏览器后重试。');}
};
function edit(item){rememberList();hideSecret();editing=item;$('vault').hidden=true;$('editor').hidden=false;$('editForm').reset();$('editTitle').textContent=item?'编辑密码':'新建密码';$('category').value=item?item.category:category||'work';for(const id of ['title','username','target','note'])$(id).value=item?(item[id]||''):'';if(!item&&category==='web'&&webAccount!==null)$('username').value=webAccount;if(!item&&category==='app'&&appName!==null)$('target').value=appName;if(!item&&category==='work'&&workName!==null)$('target').value=workName;$('password').required=!item;$('password').type=superMode?'text':'password';if(item&&(superMode||isCode(item)))$('password').value=item.password||'';$('keepHint').textContent=item?'密码留空表示保留原密码；保存后即可同步。':'保存后即可在设备点击同步。';targetLabel();window.scrollTo(0,0);}
$('loginForm').onsubmit=e=>{e.preventDefault();attempt(async()=>{const password=$('admin').value,confirm=$('confirm').value;$('admin').value='';$('confirm').value='';await api(initialized?'login':'setup',initialized?{password}:{password,confirm});initialized=true;lastActivity=Date.now();notice();await reload();});};
$('logout').onclick=lockVault;
$('add').onclick=()=>edit(null);$('editCancel').onclick=()=>{$('editForm').reset();render();};$('category').onchange=targetLabel;$('search').oninput=render;$('refresh').onclick=()=>attempt(reload);
$('editForm').onsubmit=e=>{e.preventDefault();attempt(async()=>{const body={revision,category:$('category').value};if(editing)body.id=editing.id;for(const id of ['title','username','password','target','note'])body[id]=$(id).value;if(body.category==='other')delete body.username;const labels=new Map(entryFields(body.category));for(const [id,limit] of [['title',96],['target',192],['note',256]])if(new TextEncoder().encode(body[id]).length>limit)throw new Error(labels.get(id)+`超过${limit}字节，请缩短后保存`);const r=await api('entries',body);$('editForm').reset();editing=null;selected=r.entry;category=selected.category;notice('已保存，可在设备同步');await reload();});};
$('revealCancel').onclick=hideSecret;$('revealDialog').addEventListener('close',()=>{epoch++;$('revealAdmin').value='';});
$('revealForm').onsubmit=async e=>{e.preventDefault();const id=selected?.id,version=epoch,password=$('revealAdmin').value;$('revealAdmin').value='';try{const r=await api('entries/'+id+'/reveal',{password});if(epoch!==version||selected?.id!==id||!$('revealDialog').open)return;$('secret').textContent=r.password;r.password='';$('showSecret').textContent='隐藏密码';$('revealDialog').close();secretTimer=setTimeout(hideSecret,30000);}catch(error){if(!error.stale && epoch===version && $('revealDialog').open)$('revealError').textContent=error.message;}};
document.addEventListener('visibilitychange',()=>{if(document.hidden){hideSecret();clearImport();clearArchive();clearChange();}});for(const event of ['pointerdown','keydown'])document.addEventListener(event,()=>lastActivity=Date.now());setInterval(()=>{if(!$('gate').hidden||Date.now()-lastActivity<300000)return;lockVault();},10000);
let changeEpoch=0,changeBusy=false;
function setChangeBusy(busy){
  changeBusy=busy;
  for(const id of ['changeOld','changeNew','changeConfirm','changeSubmit','changeCancel'])$(id).disabled=busy;
}
function clearChange(){
  changeEpoch++;$('changeForm').reset();$('changeError').textContent='';
  $('changeDialog').close();setChangeBusy(false);
}
$('changeAdmin').onclick=()=>{
  hideSecret();clearChange();$('changeDialog').showModal();$('changeOld').focus();
};
$('changeCancel').onclick=clearChange;
$('changeDialog').addEventListener('cancel',e=>{e.preventDefault();if(!changeBusy)clearChange();});
$('changeForm').onsubmit=async e=>{
  e.preventDefault();if(changeBusy)return;
  const body={password:$('changeOld').value,new_password:$('changeNew').value,confirm:$('changeConfirm').value};
  const version=changeEpoch;
  $('changeError').textContent='';
  if(body.new_password!==body.confirm){$('changeError').textContent='两次新密码不一致';$('changeNew').value='';$('changeConfirm').value='';return;}
  setChangeBusy(true);$('changeForm').reset();
  try{
    await api('admin/password',body);
    if(version!==changeEpoch)return;
    sessionEpoch++;clearChange();setSuperMode(false);hideSecret();clearImport();clearArchive();render();modeChannel?.postMessage('normal');
    notice('管理员密码修改成功，其他登录已失效。设备数字密码不变。');
  }catch(error){
    if(!error.stale&&version===changeEpoch){
      if(error.status===401){gate();notice(error.message);}
      else $('changeError').textContent=error.status?error.message:'连接中断，保存结果未确认，请使用新密码重新登录核对。';
    }
  }finally{
    body.password=body.new_password=body.confirm='';
    if(version===changeEpoch)setChangeBusy(false);
  }
};
const CSV_LIMIT=128*1024;
let importEpoch=0,importReady=null;
function importBusy(busy,committing=false){
  for(const id of ['importFile','importPreviewButton'])$(id).disabled=busy;
  $('importCommit').disabled=busy||!importReady;
  $('importCancel').disabled=committing;
}
function invalidateImport(){
  importEpoch++;importReady=null;
  $('importPreview').hidden=true;$('importRows').replaceChildren();$('importErrors').replaceChildren();$('importSkipped').replaceChildren();
  $('importSummary').textContent='';$('importError').textContent='';importBusy(false);
}
function clearImport(){invalidateImport();$('importForm').reset();}
function openImport(){
  if(category!=='web')return;
  rememberList();
  clearArchive();$('archivePanel').hidden=true;hideSecret();clearImport();notice();
  $('vault').hidden=true;$('editor').hidden=true;$('importer').hidden=false;
  $('importFile').focus();
}
function showImportReport(report){
  setSuperMode(superMode&&report.super_mode===true);
  $('importPreview').hidden=false;
  $('importSummary').textContent=`原始 ${report.rows} 行 → ${report.unique} 组；跳过无账号 ${report.skipped_empty} 行、不兼容 ${report.skipped_entries.length} 行，重复合并 ${report.merged} 行。新增 ${report.added} 条，更新 ${report.updated} 条，无变化 ${report.unchanged} 条。导入后共 ${report.total}/${maxEntries} 条。`;
  const skipped=$('importSkipped');skipped.replaceChildren();
  for(const item of report.skipped_entries)skipped.append(node('li',`第 ${item.row} 行：${item.message}`));
  const rows=$('importRows');rows.replaceChildren();
  const actions={added:'新增',updated:'合并更新',unchanged:'无变化'};
  for(const item of report.entries){
    const card=node('div',undefined,'import-entry'),heading=node('div',undefined,'import-heading');
    heading.append(node('strong',item.title),node('small',actions[item.action]));
    card.append(heading,node('div','网址：'+item.target,'import-value'));
    if(isCode(item)&&typeof item.password==='string')card.append(node('div','码值：'+item.password,'code-value'));else if(superMode&&typeof item.password==='string')card.append(node('div',item.password,'plain-password'));
    if(item.note)card.append(node('div','备注：'+item.note,'import-value'));
    rows.append(card);
  }
  const errors=$('importErrors');errors.replaceChildren();
  for(const error of report.errors)errors.append(node('li',(error.row?`第 ${error.row} 行：`:'')+error.message));
  if(report.error_count>report.errors.length)errors.append(node('li',`另有 ${report.error_count-report.errors.length} 项错误，请调整文件后重试。`));
  if(!report.can_import)$('importError').textContent=report.skipped===report.rows?'没有可导入的记录，已跳过全部记录。':'检查未通过，本次不会写入。请处理以上错误后重新预览。';
}
$('importOpen').onclick=openImport;
$('importCancel').onclick=()=>{clearImport();render();};
$('importFile').onchange=invalidateImport;
$('importForm').onsubmit=async event=>{
  event.preventDefault();invalidateImport();
  const file=$('importFile').files[0],type='web',version=importEpoch;
  if(!type||!file){$('importError').textContent='请选择CSV文件';return;}
  if(file.size>CSV_LIMIT){$('importError').textContent='CSV文件不能超过128 KiB';return;}
  importBusy(true);
  try{
    let csv;
    try{csv=new TextDecoder('utf-8',{fatal:true}).decode(await file.arrayBuffer());}
    catch(error){throw new Error('无法读取文件，请选择Edge导出的UTF-8 CSV文件');}
    if(version!==importEpoch)return;
    const report=await api('entries/import',{category:type,csv,preview:true});
    if(version!==importEpoch)return;
    showImportReport(report);
    if(report.can_import)importReady={category:type,csv,revision:report.revision};
    csv='';
  }catch(error){
    if(!error.stale&&version===importEpoch){$('importError').textContent=error.status===404?'导入接口未加载，请重启服务端并刷新页面':error.message;if(error.status===401)gate();}
  }finally{if(version===importEpoch)importBusy(false);}
};
$('importCommit').onclick=async()=>{
  if(!importReady)return;
  const version=importEpoch,type=importReady.category,body={...importReady,preview:false};
  importReady=null;importBusy(true,true);$('importError').textContent='';
  try{
    const report=await api('entries/import',body);
    if(version!==importEpoch)return;
    clearImport();category=type;webAccount=null;selected=null;$('search').value='';
    notice(`导入完成：新增 ${report.added} 条，更新 ${report.updated} 条，无变化 ${report.unchanged} 条，跳过无账号 ${report.skipped_empty} 行、不兼容 ${report.skipped_entries.length} 行。已按账号排序，可在设备点击同步。`);
    await reload();
  }catch(error){
    if(!error.stale&&version===importEpoch){
      $('importError').textContent=error.status?error.message:'连接异常，请刷新列表确认是否已导入，再重新预览。';
      if(error.status===401)gate();
    }
  }finally{body.csv='';if(version===importEpoch)importBusy(false);}
};



// Whole-vault transfer never decrypts passwords in the browser.
const ARCHIVE_LIMIT=256*1024;
let archiveEpoch=0,archiveReady=null,archiveExportBusy=false,archiveBusy=false;
function setArchiveBusy(busy,committing=false){
  archiveBusy=busy;
  for(const id of ['archiveFile','archivePreview'])$(id).disabled=busy;
  $('archiveCommit').disabled=busy||!archiveReady;
  $('archiveCancel').disabled=committing;
}
function invalidateArchive(){
  archiveEpoch++;archiveReady=null;
  $('archiveResult').hidden=true;$('archiveRows').replaceChildren();
  $('archiveSummary').textContent='';$('archiveError').textContent='';setArchiveBusy(false);
}
function clearArchive(){invalidateArchive();$('archiveForm').reset();}
$('archiveOpen').onclick=()=>{
  rememberList();
  hideSecret();clearImport();clearArchive();notice();
  $('vault').hidden=true;$('editor').hidden=true;$('importer').hidden=true;
  $('archivePanel').hidden=false;$('archiveFile').focus();
};
$('archiveCancel').onclick=()=>{clearArchive();render();};
$('archiveFile').onchange=invalidateArchive;
$('archiveExport').onclick=async()=>{
  if(archiveExportBusy)return;
  const expected=sessionEpoch,version=archiveEpoch;
  archiveExportBusy=true;$('archiveExport').disabled=true;hideSecret();notice();
  try{
    const response=await fetch('/api/archive/export',{method:'POST',credentials:'same-origin',cache:'no-store'});
    if(!response.ok){const data=await response.json();const error=new Error(data.detail||'导出失败');error.status=response.status;throw error;}
    const blob=await response.blob();
    if(expected!==sessionEpoch||version!==archiveEpoch||document.hidden)return;
    const url=URL.createObjectURL(blob),link=document.createElement('a');
    link.href=url;link.download='joyvault-'+new Date().toISOString().replace(/[:.]/g,'-')+'.jyvault';
    document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
    notice('已生成全部密码的加密文件，请查看浏览器下载。恢复时需使用相同部署密钥；不含管理员密码。');
  }catch(error){if(expected===sessionEpoch&&version===archiveEpoch){if(error.status===401)gate();notice(error.message);}}
  finally{archiveExportBusy=false;$('archiveExport').disabled=false;}
};
$('archiveForm').onsubmit=async event=>{
  event.preventDefault();if(archiveBusy)return;invalidateArchive();
  const file=$('archiveFile').files[0],version=archiveEpoch;
  if(!file){$('archiveError').textContent='请选择加密密码文件';return;}
  if(file.size<144||file.size>ARCHIVE_LIMIT){$('archiveError').textContent='请选择不超过256 KiB的有效加密密码文件';return;}
  setArchiveBusy(true);let encoded='';
  try{
    const bytes=new Uint8Array(await file.arrayBuffer());
    if(version!==archiveEpoch)return;
    let binary='';for(let i=0;i<bytes.length;i+=8192)binary+=String.fromCharCode(...bytes.subarray(i,i+8192));
    encoded=btoa(binary);binary='';
    const report=await api('archive/import',{file:encoded,preview:true});
    if(version!==archiveEpoch)return;
    setSuperMode(superMode&&report.super_mode===true);$('archiveResult').hidden=false;
    $('archiveSummary').textContent=`文件 ${report.rows} 条 → ${report.unique} 组，文件内重复 ${report.merged} 条；新增 ${report.added} 条，合并更新 ${report.updated} 条，无变化 ${report.unchanged} 条。导入后 ${report.total}/${maxEntries} 条。`;
    const actions={added:'新增',updated:'合并更新',unchanged:'无变化'};
    for(const item of report.entries){
      const card=node('div',undefined,'import-entry'),heading=node('div',undefined,'import-heading');
      heading.append(node('strong',item.title),node('small',actions[item.action]));card.append(heading);
      if(isCode(item)&&typeof item.password==='string')card.append(node('div','码值：'+item.password,'code-value'));else if(superMode&&typeof item.password==='string')card.append(node('div',item.password,'plain-password'));
      card.append(node('div',names[item.category]+(isCode(item)?'':' · '+(item.username||'无账号')),'import-value'));
      if(item.target)card.append(node('div',TARGET_FIELDS[item.category].label+'：'+item.target,'import-value'));
      if(item.note)card.append(node('div','备注：'+item.note,'import-value'));
      $('archiveRows').append(card);
    }
    if(report.can_import)archiveReady={file:encoded,revision:report.revision};
  }catch(error){if(!error.stale&&version===archiveEpoch){if(error.status===401)gate();else $('archiveError').textContent=error.message;}}
  finally{encoded='';if(version===archiveEpoch)setArchiveBusy(false);}
};
$('archiveCommit').onclick=async()=>{
  if(!archiveReady||archiveBusy)return;
  const version=archiveEpoch,body={...archiveReady,preview:false};
  archiveReady=null;setArchiveBusy(true,true);$('archiveError').textContent='';
  try{
    const report=await api('archive/import',body);
    if(version!==archiveEpoch)return;
    clearArchive();category=null;webAccount=null;selected=null;$('search').value='';
    notice(`加密导入完成：新增 ${report.added} 条，合并更新 ${report.updated} 条，无变化 ${report.unchanged} 条。请在设备点击同步。`);
    await reload();
  }catch(error){if(!error.stale&&version===archiveEpoch){if(error.status===401)gate();else $('archiveError').textContent=error.status?error.message:'连接异常，请刷新列表核对是否已导入，再重新预览。';}}
  finally{body.file='';if(version===archiveEpoch)setArchiveBusy(false);}
};

attempt(async()=>{initialized=(await api('status')).initialized;gate();await api('logout',{});});
